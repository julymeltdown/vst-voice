#pragma once
#include "onnx/onnx.pb.h"
#include <map>
#include <stdexcept>
#include <string>

// Structural export profile only; not an executable admission handle.
inline bool pairContract(const onnx::ModelProto& acoustic,const onnx::ModelProto& vocoder,
    unsigned bins,const std::string& layout,const std::string& steps,const std::string& output,
    unsigned hop,unsigned maximumFrames) {
  if (!bins || bins>512 || !hop || hop>8192 || !maximumFrames || maximumFrames>4194304 ||
      (layout!="BTF" && layout!="BFT") || (steps!="scalar" && steps!="vector1") ||
      (output!="audio" && output!="waveform") ||
      ((static_cast<std::uint64_t>(maximumFrames)+hop-1)/hop)*bins>64U*1024U*1024U) return false;
  try {
    using Map=std::map<std::string,const onnx::ValueInfoProto*>;
    const auto entries=[](const auto& values,std::initializer_list<std::string> names) {
      Map result;
      for (const auto& value:values)
        if (!result.emplace(value.name(),&value).second) throw std::runtime_error("duplicate interface");
      if (result.size()!=names.size()) throw std::runtime_error("unexpected interface");
      for (const auto& name:names) if (!result.contains(name)) throw std::runtime_error("missing interface");
      return result;
    };
    const auto shape=[](const onnx::ValueInfoProto* value,int dtype,int rank)->const onnx::TensorShapeProto& {
      const auto& type=value->type().tensor_type();
      if (!value->type().has_tensor_type() || type.elem_type()!=dtype || !type.has_shape() || type.shape().dim_size()!=rank)
        throw std::runtime_error("interface type or rank");
      return type.shape();
    };
    const auto fixed=[](const auto& dim,unsigned value) {return dim.has_dim_value() && dim.dim_value()==value;};
    const auto symbol=[](const auto& dim) {
      if (!dim.has_dim_param() || dim.dim_param().empty()) throw std::runtime_error("missing symbolic axis");
      return dim.dim_param();
    };
    const auto sequence=[&](const auto* value,int dtype) {
      const auto& dims=shape(value,dtype,2);
      if (!fixed(dims.dim(0),1)) throw std::runtime_error("sequence batch");
      return symbol(dims.dim(1));
    };
    const auto mel=[&](const auto* value) {
      const auto& dims=shape(value,onnx::TensorProto::FLOAT,3);
      if (!fixed(dims.dim(0),1) || !fixed(dims.dim(layout=="BTF"?2:1),bins)) throw std::runtime_error("mel geometry");
      return symbol(dims.dim(layout=="BTF"?1:2));
    };
    const auto ai=entries(acoustic.graph().input(),{"tokens","durations","f0","steps"});
    const auto ao=entries(acoustic.graph().output(),{"mel"});
    const auto tokenAxis=sequence(ai.at("tokens"),onnx::TensorProto::INT64);
    const auto timeAxis=sequence(ai.at("f0"),onnx::TensorProto::FLOAT);
    if (sequence(ai.at("durations"),onnx::TensorProto::INT64)!=tokenAxis ||
        tokenAxis==timeAxis || mel(ao.at("mel"))!=timeAxis) return false;
    const auto& stepShape=shape(ai.at("steps"),onnx::TensorProto::INT64,steps=="scalar"?0:1);
    if (steps=="vector1" && !fixed(stepShape.dim(0),1)) return false;
    const auto vi=entries(vocoder.graph().input(),{"mel","f0"});
    const auto vo=entries(vocoder.graph().output(),{output});
    const auto vocoderAxis=mel(vi.at("mel"));
    return sequence(vi.at("f0"),onnx::TensorProto::FLOAT)==vocoderAxis &&
        sequence(vo.at(output),onnx::TensorProto::FLOAT)!=vocoderAxis;
  } catch (const std::runtime_error&) {return false;}
}
