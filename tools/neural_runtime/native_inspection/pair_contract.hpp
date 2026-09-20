#pragma once
#include "onnx/onnx.pb.h"
#include <functional>
#include <map>
#include <set>
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
      auto expected=names.begin();
      for (const auto& value:values)
        if (!result.emplace(value.name(),&value).second) throw std::runtime_error("duplicate interface");
        else if (expected==names.end() || value.name()!=*expected++) throw std::runtime_error("interface order");
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
    Map ai;
    // The worker binds inputs positionally, so the declared order is part of
    // the contract: tokens, durations, f0, steps, then the optional control.
    const std::initializer_list<const char*> acousticOrder=
        {"tokens","durations","f0","steps","breathiness"};
    auto acousticExpected=acousticOrder.begin();
    for (const auto& value:acoustic.graph().input()) {
      if (!ai.emplace(value.name(),&value).second) throw std::runtime_error("duplicate interface");
      if (acousticExpected==acousticOrder.end() || value.name()!=*acousticExpected++)
        throw std::runtime_error("interface order");
    }
    const bool conditioned=ai.contains("breathiness");
    if (ai.size()!=(conditioned?5U:4U)) throw std::runtime_error("unexpected interface");
    for (const auto* name:{"tokens","durations","f0","steps"})
      if (!ai.contains(name)) throw std::runtime_error("missing interface");
    const auto ao=entries(acoustic.graph().output(),{"mel"});
    const auto tokenAxis=sequence(ai.at("tokens"),onnx::TensorProto::INT64);
    const auto timeAxis=sequence(ai.at("f0"),onnx::TensorProto::FLOAT);
    if (sequence(ai.at("durations"),onnx::TensorProto::INT64)!=tokenAxis ||
        tokenAxis==timeAxis || mel(ao.at("mel"))!=timeAxis) return false;
    if (conditioned) {
      if (sequence(ai.at("breathiness"),onnx::TensorProto::FLOAT)!=timeAxis) return false;
      const std::map<std::string,std::string> required{
          {"seam.conditioning.revision","2"},
          {"seam.conditioning.breathiness.type","float32"},
          {"seam.conditioning.breathiness.unit","normalized-periodic-aperiodic-balance"},
          {"seam.conditioning.breathiness.minimum","0"},
          {"seam.conditioning.breathiness.maximum","1"},
          {"seam.conditioning.breathiness.default","0"},
          {"seam.conditioning.breathiness.supported","true"}};
      std::map<std::string,std::string> metadata;
      for (const auto& entry:acoustic.metadata_props())
        if (!metadata.emplace(entry.key(),entry.value()).second) return false;
      for (const auto& [key,value]:required)
        if (!metadata.contains(key) || metadata.at(key)!=value) return false;
      for (const auto& [key,value]:metadata)
        if (key.starts_with("seam.conditioning.") && !required.contains(key)) return false;
      // ONNX control-flow bodies capture enclosing values implicitly. Make those lexical captures
      // explicit for the reachability proof; otherwise a real DiffSinger If/Loop graph would look
      // as though it discarded the encoder condition at the branch boundary.
      std::function<std::set<std::string>(const onnx::GraphProto&)> captures;
      captures=[&](const onnx::GraphProto& graph) {
        std::set<std::string> definitions,uses;
        for (const auto& input:graph.input()) definitions.insert(input.name());
        for (const auto& initializer:graph.initializer()) definitions.insert(initializer.name());
        for (const auto& node:graph.node()) {
          for (const auto& output:node.output()) if (!output.empty()) definitions.insert(output);
          for (const auto& input:node.input()) if (!input.empty()) uses.insert(input);
          for (const auto& attribute:node.attribute()) {
            if (attribute.has_g()) {
              const auto nested=captures(attribute.g());
              uses.insert(nested.begin(),nested.end());
            }
            for (const auto& nestedGraph:attribute.graphs()) {
              const auto nested=captures(nestedGraph);
              uses.insert(nested.begin(),nested.end());
            }
          }
        }
        for (const auto& definition:definitions) uses.erase(definition);
        return uses;
      };
      const auto consumes=[&](const onnx::NodeProto& node,const std::set<std::string>& reachable) {
        for (const auto& input:node.input()) if (reachable.contains(input)) return true;
        for (const auto& attribute:node.attribute()) {
          if (attribute.has_g())
            for (const auto& capture:captures(attribute.g())) if (reachable.contains(capture)) return true;
          for (const auto& nestedGraph:attribute.graphs())
            for (const auto& capture:captures(nestedGraph)) if (reachable.contains(capture)) return true;
        }
        return false;
      };
      std::set<std::string> reachable{"breathiness"};
      bool changed=true;
      while (changed) {
        changed=false;
        for (const auto& node:acoustic.graph().node()) {
          if (!consumes(node,reachable)) continue;
          for (const auto& name:node.output()) changed=reachable.insert(name).second || changed;
        }
      }
      if (!reachable.contains("mel")) return false;
    } else {
      for (const auto& entry:acoustic.metadata_props())
        if (entry.key().starts_with("seam.conditioning.")) return false;
    }
    const auto& stepShape=shape(ai.at("steps"),onnx::TensorProto::INT64,steps=="scalar"?0:1);
    if (steps=="vector1" && !fixed(stepShape.dim(0),1)) return false;
    const auto vi=entries(vocoder.graph().input(),{"mel","f0"});
    const auto vo=entries(vocoder.graph().output(),{output});
    const auto vocoderAxis=mel(vi.at("mel"));
    return sequence(vi.at("f0"),onnx::TensorProto::FLOAT)==vocoderAxis &&
        sequence(vo.at(output),onnx::TensorProto::FLOAT)!=vocoderAxis;
  } catch (const std::runtime_error&) {return false;}
}
