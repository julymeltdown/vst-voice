#include "inspection.hpp"

int main() {
  onnx::ModelProto source;
  source.set_ir_version(9); source.add_opset_import()->set_version(17);
  auto* graph=source.mutable_graph(); graph->set_name("owned-bytes");
  const auto interface=[](onnx::ValueInfoProto* value,const char* name) {
    value->set_name(name);
    auto* tensor=value->mutable_type()->mutable_tensor_type(); tensor->set_elem_type(1);
    tensor->mutable_shape()->add_dim()->set_dim_value(1);
  };
  interface(graph->add_input(),"input"); interface(graph->add_output(),"output");
  auto* node=graph->add_node(); node->set_op_type("Identity");
  node->add_input("input"); node->add_output("output");
  auto bytes=source.SerializeAsString();
  onnx::ModelProto parsed; google::protobuf::Struct report;
  if (inspectBytes(bytes,parsed,report)!=0) return 1;
  bytes.assign(bytes.size(),'x');
  if (parsed.graph().input(0).name()!="input") return 2;
  const auto before=parsed.SerializeAsString(),reportBefore=report.SerializeAsString();
  if (inspectBytes(bytes,parsed,report)==0) return 3;
  if (parsed.SerializeAsString()!=before || report.SerializeAsString()!=reportBefore) return 4;
  if (inspectBytes({},parsed,report)!=3) return 5;
  if (parsed.SerializeAsString()!=before || report.SerializeAsString()!=reportBefore) return 6;
  std::stop_source cancelled; cancelled.request_stop();
  if (inspectBytes(source.SerializeAsString(),parsed,report,cancelled.get_token())!=18) return 7;
  if (parsed.SerializeAsString()!=before || report.SerializeAsString()!=reportBefore) return 8;
}
