// Native parser experiment only. No sessions, operator admission or file resolution.
#include "onnx/onnx.pb.h"
#include "onnx/checker.h"
#include "pair_contract.hpp"
#include "inspection.hpp"
#include <charconv>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cmath>

unsigned elementBytes(int type) {
  switch (type) {
    case onnx::TensorProto::FLOAT: case onnx::TensorProto::INT32: return 4;
    case onnx::TensorProto::DOUBLE: case onnx::TensorProto::INT64: return 8;
    case onnx::TensorProto::FLOAT16: return 2;
    case onnx::TensorProto::INT8: case onnx::TensorProto::UINT8: case onnx::TensorProto::BOOL: return 1;
    default: return 0;
  }
}

bool validValues(const onnx::TensorProto& tensor) {
  const auto type=tensor.data_type();
  if (tensor.has_raw_data()) {
    const auto width=elementBytes(type);
    const auto& data=tensor.raw_data();
    for (std::size_t offset=0;offset<data.size();offset+=width) {
      // ONNX raw tensor storage is little-endian, independent of host order.
      std::uint64_t bits=0;
      for (unsigned byte=0;byte<width;++byte)
        bits|=static_cast<std::uint64_t>(static_cast<unsigned char>(data[offset+byte]))<<(8U*byte);
      if ((type==onnx::TensorProto::FLOAT && (bits&0x7f800000U)==0x7f800000U) ||
          (type==onnx::TensorProto::DOUBLE && (bits&0x7ff0000000000000ULL)==0x7ff0000000000000ULL) ||
          (type==onnx::TensorProto::FLOAT16 && (bits&0x7c00U)==0x7c00U) ||
          (type==onnx::TensorProto::BOOL && bits>1U)) return false;
    }
  } else {
    for (const auto value:tensor.float_data()) if (!std::isfinite(value)) return false;
    for (const auto value:tensor.double_data()) if (!std::isfinite(value)) return false;
    for (const auto value:tensor.int32_data()) {
      if ((type==onnx::TensorProto::BOOL && (value<0 || value>1)) ||
          (type==onnx::TensorProto::INT8 && (value<-128 || value>127)) ||
          (type==onnx::TensorProto::UINT8 && (value<0 || value>255)) ||
          (type==onnx::TensorProto::FLOAT16 && (value<0 || value>65535 || (value&0x7c00)==0x7c00))) return false;
    }
  }
  return true;
}

static int inspectModelBytes(std::span<const char> bytes,onnx::ModelProto& model,google::protobuf::Struct& report) {
  constexpr int maximumBytes=256*1024*1024;
  if (bytes.empty() || bytes.size()>maximumBytes) return 3;
  google::protobuf::io::ArrayInputStream raw{bytes.data(),static_cast<int>(bytes.size())};
  google::protobuf::io::CodedInputStream coded{&raw};
  coded.SetTotalBytesLimit(maximumBytes); coded.SetRecursionLimit(64);
  if (!model.ParseFromCodedStream(&coded) || !coded.ConsumedEntireMessage()) return 4;
  if (!model.has_graph() || model.ir_version()<1 || model.ir_version()>10 ||
      model.functions_size() || model.training_info_size() || model.opset_import_size()!=1 ||
      !model.opset_import(0).domain().empty() || model.opset_import(0).version()<13 ||
      model.opset_import(0).version()>21) return 5;
  std::vector<const google::protobuf::Message*> pending{&model};
  std::size_t visited=0,nodes=0,tensors=0;
  std::uint64_t declaredBytes=0;
  constexpr std::uint64_t maximumElements=64U*1024U*1024U,maximumTensorBytes=512U*1024U*1024U;
  while (!pending.empty()) {
    const auto* message=pending.back(); pending.pop_back();
    if (++visited>200000) return 6;
    const auto* reflection=message->GetReflection();
    if (reflection->GetUnknownFields(*message).field_count()!=0) return 7;
    if (const auto* node=dynamic_cast<const onnx::NodeProto*>(message)) {
      ++nodes;
      if (!node->domain().empty() || !node->overload().empty() || node->op_type().empty()) return 8;
    }
    if (const auto* tensor=dynamic_cast<const onnx::TensorProto*>(message)) {
      ++tensors;
      if (tensor->external_data_size() || tensor->data_location()==onnx::TensorProto::EXTERNAL) return 9;
      const auto width=elementBytes(tensor->data_type());
      if (!width || tensor->dims_size()>8) return 10;
      std::uint64_t elements=1;
      for (const auto dimension:tensor->dims()) {
        if (dimension<0 || static_cast<std::uint64_t>(dimension)>maximumElements ||
            (dimension && elements>maximumElements/static_cast<std::uint64_t>(dimension))) return 10;
        elements*=static_cast<std::uint64_t>(dimension);
      }
      const auto storage=elements*width;
      if (storage>maximumTensorBytes-declaredBytes) return 11;
      declaredBytes+=storage;
      if (tensor->has_segment()) return 13;
      const std::uint64_t typedCount=static_cast<std::uint64_t>(tensor->float_data_size())+
          tensor->double_data_size()+tensor->int32_data_size()+tensor->int64_data_size()+
          tensor->uint64_data_size()+tensor->string_data_size();
      if (tensor->has_raw_data()) {
        if (typedCount || tensor->raw_data().size()!=storage) return 13;
      } else {
        std::uint64_t expectedCount=0;
        switch (tensor->data_type()) {
          case onnx::TensorProto::FLOAT: expectedCount=tensor->float_data_size(); break;
          case onnx::TensorProto::DOUBLE: expectedCount=tensor->double_data_size(); break;
          case onnx::TensorProto::INT64: expectedCount=tensor->int64_data_size(); break;
          default: expectedCount=tensor->int32_data_size(); break;
        }
        if (expectedCount!=elements || typedCount!=expectedCount) return 13;
      }
      if (!validValues(*tensor)) return 14;
    }
    if (const auto* value=dynamic_cast<const onnx::ValueInfoProto*>(message)) {
      if (!value->has_type() || !value->type().has_tensor_type()) return 12;
      const auto& tensor=value->type().tensor_type();
      if (!elementBytes(tensor.elem_type()) || !tensor.has_shape() || tensor.shape().dim_size()>8) return 12;
      std::uint64_t elements=1;
      for (const auto& dimension:tensor.shape().dim()) {
        if (dimension.has_dim_value()) {
          const auto size=dimension.dim_value();
          if (size<0 || static_cast<std::uint64_t>(size)>maximumElements ||
              (size && elements>maximumElements/static_cast<std::uint64_t>(size))) return 12;
          elements*=static_cast<std::uint64_t>(size);
        } else if (!dimension.has_dim_param() || dimension.dim_param().empty()) return 12;
      }
    }
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(*message,&fields);
    for (const auto* field:fields) {
      if (field->cpp_type()!=google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) continue;
      const auto count=field->is_repeated()?reflection->FieldSize(*message,field):1;
      if (pending.size()+visited+static_cast<std::size_t>(count)>200000) return 6;
      for (int index=0;index<count;++index)
        pending.push_back(field->is_repeated()?&reflection->GetRepeatedMessage(*message,field,index):
            &reflection->GetMessage(*message,field));
    }
  }
  // External data was rejected throughout the message tree before calling the
  // upstream in-memory checker. Never call its filename-loading overload.
  try { onnx::checker::check_model(model,false); }
  catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 15; }
  auto& fields=*report.mutable_fields();
  fields["status"].set_string_value("NATIVE_STRUCTURE_CHECKED");
  fields["nodes"].set_number_value(nodes);
  fields["tensors"].set_number_value(tensors);
  fields["declaredTensorBytes"].set_number_value(declaredBytes);
  fields["irVersion"].set_number_value(model.ir_version());
  fields["opset"].set_number_value(model.opset_import(0).version());
  fields["executionAdmitted"].set_bool_value(false);
  fields["releaseEligible"].set_bool_value(false);
  const auto interface=[&](const auto& values,const char* key) {
    auto* list=fields[key].mutable_list_value();
    for (const auto& value:values) {
      auto& entry=*list->add_values()->mutable_struct_value()->mutable_fields();
      entry["name"].set_string_value(value.name());
      entry["dtype"].set_number_value(value.type().tensor_type().elem_type());
      auto* shape=entry["shape"].mutable_list_value();
      for (const auto& dimension:value.type().tensor_type().shape().dim()) {
        auto* item=shape->add_values();
        if (dimension.has_dim_value()) item->set_number_value(dimension.dim_value());
        else item->set_string_value(dimension.dim_param());
      }
    }
  };
  interface(model.graph().input(),"inputs"); interface(model.graph().output(),"outputs");
  return 0;
}

int inspectBytes(std::span<const char> bytes,onnx::ModelProto& model,google::protobuf::Struct& report) {
  onnx::ModelProto candidate; google::protobuf::Struct candidateReport;
  const auto result=inspectModelBytes(bytes,candidate,candidateReport);
  if (result) return result;
  model.Swap(&candidate); report.Swap(&candidateReport);
  return 0;
}

static int inspect(const char* path,onnx::ModelProto& model,google::protobuf::Struct& report) {
  std::ifstream input{path,std::ios::binary|std::ios::ate};
  const auto size=input.tellg();
  if (!input || size<=0 || size>256*1024*1024) return 3;
  std::vector<char> bytes(static_cast<std::size_t>(size));
  input.seekg(0); input.read(bytes.data(),size);
  if (!input || input.peek()!=std::char_traits<char>::eof()) return 3;
  return inspectBytes(bytes,model,report);
}

int runInspectionCommand(int argc,char** argv) {
  const bool paired=argc==10 && std::string_view{argv[1]}=="--pair";
  if (argc!=2 && !paired) return 2;
  onnx::ModelProto model;
  google::protobuf::Struct report;
  const auto result=inspect(argv[paired?2:1],model,report);
  if (result) return result;
  if (paired) {
    unsigned bins{},hop{},frames{};
    const auto integer=[](const char* text,unsigned& value) {
      const std::string_view input{text};
      const auto parsed=std::from_chars(input.data(),input.data()+input.size(),value);
      return parsed.ec==std::errc{} && parsed.ptr==input.data()+input.size();
    };
    if (!integer(argv[4],bins) || !integer(argv[8],hop) || !integer(argv[9],frames)) return 17;
    onnx::ModelProto vocoder; google::protobuf::Struct vocoderReport;
    const auto checked=inspect(argv[3],vocoder,vocoderReport); if (checked) return checked;
    if (!pairContract(model,vocoder,bins,argv[5],argv[6],argv[7],hop,frames)) return 17;
    report.Clear(); auto& fields=*report.mutable_fields();
    fields["status"].set_string_value("NATIVE_PAIR_STRUCTURE_CHECKED");
    fields["executionAdmitted"].set_bool_value(false); fields["releaseEligible"].set_bool_value(false);
  }
  std::string json;
  if (!google::protobuf::util::MessageToJsonString(report,&json).ok()) return 16;
  std::cout<<json<<'\n';
  return std::cout?0:16;
}
