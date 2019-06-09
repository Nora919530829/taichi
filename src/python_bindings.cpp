#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <taichi/python/export.h>
#include <taichi/common/interface.h>
#include "tlang.h"

TLANG_NAMESPACE_BEGIN

Expr expr_index(const Expr &expr, const Expr &index) {
  return expr[index];
}

void expr_assign(const Expr &lhs, const Expr &rhs) {
  TC_ASSERT(lhs->is_lvalue());
  current_ast_builder().insert(
      std::make_unique<FrontendAssignStmt>(lhs, load_if_ptr(rhs)));
}

std::vector<std::unique_ptr<IRBuilder::ScopeGuard>> scope_stack;

template <typename T, typename C>
void export_accessors(C &c) {
  c.def(
      fmt::format("val1_{}", data_type_short_name(get_data_type<T>())).c_str(),
      &Expr::val<T, int>);
  c.def(
      fmt::format("val2_{}", data_type_short_name(get_data_type<T>())).c_str(),
      &Expr::val<T, int, int>);
  c.def(
      fmt::format("val3_{}", data_type_short_name(get_data_type<T>())).c_str(),
      &Expr::val<T, int, int, int>);
  c.def(
      fmt::format("val4_{}", data_type_short_name(get_data_type<T>())).c_str(),
      &Expr::val<T, int, int, int, int>);

  c.def(fmt::format("set_val1_{}", data_type_short_name(get_data_type<T>()))
            .c_str(),
        &Expr::set_val<T, int>);
  c.def(fmt::format("set_val2_{}", data_type_short_name(get_data_type<T>()))
            .c_str(),
        &Expr::set_val<T, int, int>);
  c.def(fmt::format("set_val3_{}", data_type_short_name(get_data_type<T>()))
            .c_str(),
        &Expr::set_val<T, int, int, int>);
  c.def(fmt::format("set_val4_{}", data_type_short_name(get_data_type<T>()))
            .c_str(),
        &Expr::set_val<T, int, int, int, int>);
}

PYBIND11_MODULE(taichi_lang_core, m) {
  py::enum_<Arch>(m, "Arch", py::arithmetic())
      .value("gpu", Arch::gpu)
      .value("x86_64", Arch::x86_64)
      .export_values();

  py::class_<CompileConfig>(m, "CompileConfig")
      .def(py::init<>())
      .def_readwrite("arch", &CompileConfig::arch)
      .def_readwrite("print_ir", &CompileConfig::print_ir);

  m.def(
      "default_compile_config",
      [&]() -> CompileConfig & { return default_compile_config; },
      py::return_value_policy::reference);

  py::class_<Program>(m, "Program")
      .def(py::init<>())
      .def("profiler_print", &Program::profiler_print)
      .def("profiler_print", &Program::profiler_clear);

  m.def("get_current_program", get_current_program,
        py::return_value_policy::reference);

  m.def(
      "current_compile_config",
      [&]() -> CompileConfig & { return get_current_program().config; },
      py::return_value_policy::reference);

  py::class_<Index>(m, "Index").def(py::init<int>());
  py::class_<SNode>(m, "SNode")
      .def(py::init<>())
      .def("dense",
           (SNode & (SNode::*)(const std::vector<Index> &,
                               const std::vector<int> &))(&SNode::dense),
           py::return_value_policy::reference)
      .def("pointer", &SNode::pointer)
      .def("place", (SNode & (SNode::*)(Expr &))(&SNode::place),
           py::return_value_policy::reference)
      .def("data_type", [](SNode *snode) { return snode->dt; })
      .def("num_active_indices",
           [](SNode *snode) { return snode->num_active_indices; });
  py::class_<Program::Kernel>(m, "Kernel")
      .def("__call__", &Program::Kernel::operator());

  py::class_<Expr> expr(m, "Expr");
  expr.def("serialize", &Expr::serialize)
      .def("snode", &Expr::snode, py::return_value_policy::reference)
      .def("set_tb", &Expr::set_tb);
  export_accessors<int32>(expr);
  export_accessors<float32>(expr);
  // export_accessors<float64>(expr);

  py::class_<ExprGroup>(m, "ExprGroup")
      .def(py::init<>())
      .def("push_back", &ExprGroup::push_back)
      .def("serialize", &ExprGroup::serialize);
  py::class_<Stmt>(m, "Stmt");
  py::class_<Program::KernelProxy>(m, "KernelProxy")
      .def("define", &Program::KernelProxy::def,
           py::return_value_policy::reference);

  m.def("begin_frontend_range_for",
        [&](const Expr &i, const Expr &s, const Expr &e) {
          auto stmt_unique = std::make_unique<FrontendForStmt>(i, s, e);
          auto stmt = stmt_unique.get();
          current_ast_builder().insert(std::move(stmt_unique));
          scope_stack.push_back(current_ast_builder().create_scope(stmt->body));
        });

  m.def("begin_frontend_struct_for",
        [&](const ExprGroup &indices, const Expr &global) {
          auto stmt_unique = std::make_unique<FrontendForStmt>(indices, global);
          auto stmt = stmt_unique.get();
          current_ast_builder().insert(std::move(stmt_unique));
          scope_stack.push_back(current_ast_builder().create_scope(stmt->body));
        });

  m.def("end_frontend_range_for", [&]() { scope_stack.pop_back(); });
  m.def("pop_scope", [&]() { scope_stack.pop_back(); });

  m.def("begin_frontend_if", [&](const Expr &cond) {
    auto stmt_tmp = std::make_unique<FrontendIfStmt>(cond);
    current_ast_builder().insert(std::move(stmt_tmp));
  });

  m.def("begin_frontend_if_true", [&]() {
    auto if_stmt = current_ast_builder().get_last_stmt()->as<FrontendIfStmt>();
    scope_stack.push_back(
        current_ast_builder().create_scope(if_stmt->true_statements));
  });

  m.def("begin_frontend_if_false", [&]() {
    auto if_stmt = current_ast_builder().get_last_stmt()->as<FrontendIfStmt>();
    scope_stack.push_back(
        current_ast_builder().create_scope(if_stmt->false_statements));
  });

  m.def("layout", layout);

  m.def(
      "get_root", [&]() -> SNode * { return &root; },
      py::return_value_policy::reference);

  m.def("value_cast", static_cast<Expr (*)(const Expr &expr, DataType)>(cast));

  m.def("expr_add", expr_add);
  m.def("expr_sub", expr_sub);
  m.def("expr_mul", expr_mul);
  m.def("expr_div", expr_div);
  m.def("expr_mod", expr_mod);

  m.def("expr_cmp_le", expr_cmp_le);
  m.def("expr_cmp_lt", expr_cmp_lt);
  m.def("expr_cmp_ge", expr_cmp_ge);
  m.def("expr_cmp_gt", expr_cmp_gt);
  m.def("expr_cmp_ne", expr_cmp_ne);
  m.def("expr_cmp_eq", expr_cmp_eq);

  m.def("expr_index", expr_index);

  m.def("expr_var", [](const Expr &e) { return Var(e); });
  m.def("expr_alloca", []() {
    auto var = Expr(std::make_shared<IdExpression>());
    current_ast_builder().insert(std::make_unique<FrontendAllocaStmt>(
        std::static_pointer_cast<IdExpression>(var.expr)->id,
        DataType::unknown));
    return var;
  });
  m.def("expr_assign", expr_assign);

  m.def("make_global_load_stmt", Stmt::make<GlobalLoadStmt, Stmt *>);
  m.def("make_global_store_stmt", Stmt::make<GlobalStoreStmt, Stmt *, Stmt *>);
  m.def("make_frontend_assign_stmt",
        Stmt::make<FrontendAssignStmt, const Expr &, const Expr &>);

  m.def("make_id_expr", Expr::make<IdExpression, std::string>);
  m.def("make_constant_expr", Expr::make<ConstExpression, int>);
  m.def("make_constant_expr", Expr::make<ConstExpression, float32>);
  m.def("make_constant_expr", Expr::make<ConstExpression, float64>);

  m.def("make_global_ptr_expr",
        Expr::make<GlobalPtrExpression, const Expr &, const ExprGroup &>);

  auto &&bin = py::enum_<BinaryOpType>(m, "BinaryOpType", py::arithmetic());
  for (int t = 0; t <= (int)BinaryOpType::undefined; t++)
    bin.value(binary_op_type_name(BinaryOpType(t)).c_str(), BinaryOpType(t));
  bin.export_values();
  m.def("make_binary_op_expr",
        Expr::make<BinaryOpExpression, const BinaryOpType &, const Expr &,
                   const Expr &>);

  auto &&unary = py::enum_<UnaryOpType>(m, "UnaryOpType", py::arithmetic());
  for (int t = 0; t <= (int)UnaryOpType::undefined; t++)
    unary.value(unary_op_type_name(UnaryOpType(t)).c_str(), UnaryOpType(t));
  unary.export_values();
  m.def("make_unary_op_expr",
        Expr::make<UnaryOpExpression, const UnaryOpType &, const Expr &>);

  auto &&data_type = py::enum_<DataType>(m, "DataType", py::arithmetic());
  for (int t = 0; t <= (int)DataType::unknown; t++)
    data_type.value(data_type_name(DataType(t)).c_str(), DataType(t));
  data_type.export_values();

  m.def("global_new", static_cast<Expr (*)(Expr, DataType)>(global_new));
  m.def("data_type_name", data_type_name);
  m.def("data_type_short_name", data_type_short_name);

  m.def("subscript", [](const Expr &expr, const ExprGroup &expr_group) {
    return expr[expr_group];
  });

  m.def("create_kernel", [&](std::string name) -> Program::KernelProxy {
    return get_current_program().kernel(name);
  });

  m.def("print_", Print_);

  // Schedules
  m.def("parallelize", Parallelize);
  m.def("vectorize", Vectorize);
  m.def("block_dim", BlockDim);
  m.def("cache", Cache);
}

TLANG_NAMESPACE_END
