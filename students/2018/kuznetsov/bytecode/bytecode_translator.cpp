#include "bytecode_translator.h"
#include <stdexcept>

namespace mathvm {

	Status* bytecode_translator_impl::translate(const string& program, Code* *code) {
		Parser parser;
		Status* status = parser.parseProgram(program);
		if (status->isOk()) {
			bytecode_translator visitor;
			visitor.traverse_scope(parser.top()->owner());
			*code = visitor.get_code();
			visitor.print_bytecode();
		}
		return status;
	}

	void bytecode_translator::visitBinaryOpNode(BinaryOpNode *node) {
		node->right()->visit(this);
		node->left()->visit(this);
		type_stack.push(translate_binop(node->kind()));
	}

	void bytecode_translator::visitUnaryOpNode(mathvm::UnaryOpNode *node) {
		node->operand()->visit(this);
		type_stack.push(translate_unop(node->kind()));
	}

	void bytecode_translator::visitStringLiteralNode(mathvm::StringLiteralNode *node) {
		bytecode->addInsn(Instruction::BC_SLOAD);
		bytecode->addInt16(code->makeStringConstant(node->literal()));
		type_stack.push(VT_STRING);
	}

	void bytecode_translator::visitIntLiteralNode(mathvm::IntLiteralNode *node) {
		bytecode->addInsn(Instruction::BC_ILOAD);
		bytecode->addInt64(node->literal());
		type_stack.push(VT_INT);
	}

	void bytecode_translator::visitDoubleLiteralNode(mathvm::DoubleLiteralNode *node) {
		bytecode->addInsn(Instruction::BC_DLOAD);
		bytecode->addDouble(node->literal());
		type_stack.push(VT_DOUBLE);
	}

	void bytecode_translator::visitLoadNode(mathvm::LoadNode *node) {
		translate_load(node->var());
	}

	void bytecode_translator::visitStoreNode(mathvm::StoreNode *node) {

		node->visitChildren(this);
		const AstVar* var = node->var();

		switch (node->op()) {
			case tINCRSET:
				translate_load(node->var());
				translate_binop(tADD);

				if (var->type() != type_stack.top())
					throw std::invalid_argument("type mismatch");
				type_stack.pop();
				break;
			case tDECRSET:
				translate_load(node->var());
				translate_binop(tSUB);

				if (var->type() != type_stack.top())
					throw std::invalid_argument("type mismatch");
				type_stack.pop();
				break;
			case tASSIGN:
				break;
			default:
				throw exception();
		}

		translate_store(var);
	}

	void bytecode_translator::visitBlockNode(mathvm::BlockNode *node) {
		node->visitChildren(this);
	}

	void bytecode_translator::visitIfNode(mathvm::IfNode *node) {
		node->ifExpr()->visit(this);
		resolve_int_unary(type_stack.top());
		type_stack.pop();

		Label lfalse(bytecode);
		Label lafter(bytecode);

		bytecode->addInsn(Instruction::BC_ILOAD0);
		bytecode->addBranch(Instruction::BC_IFICMPE, lfalse);
		node->thenBlock()->visit(this);
		if (node->elseBlock() != 0) {
			bytecode->addBranch(Instruction::BC_JA, lafter);
			bytecode->bind(lfalse);
			node->elseBlock()->visit(this);
			bytecode->bind(lafter);
		} else {
			bytecode->bind(lfalse);
		}
	}

	/* for (i in a..b) body
	 * ...
	 *
	 * i = a
	 * lstart:
	 * 	   if (i >= b) jmp lafter
	 * 	   body
	 * 	   i = i + 1
	 * lafter:
	 *     ...
	 * */
	void bytecode_translator::visitForNode(mathvm::ForNode *node) {
		BinaryOpNode* range_node = dynamic_cast<BinaryOpNode*>(node->inExpr());
		Label lstart(bytecode);
		Label lafter(bytecode);

		range_node->right()->visit(this);
		resolve_int_unary(type_stack.top());
		bytecode->addInsn(Instruction::BC_STOREIVAR0);
		range_node->left()->visit(this);
		resolve_int_unary(type_stack.top());
		const AstVar* var = node->var();
		translate_store(var);

		bytecode->bind(lstart);
		bytecode->addInsn(Instruction::BC_LOADIVAR0);
		translate_load(var);
		bytecode->addBranch(Instruction::BC_IFICMPG, lafter);
		node->body()->visit(this);
		bytecode->addInsn(Instruction::BC_ILOAD1);
		translate_load(var);
		bytecode->addInsn(Instruction::BC_IADD);
		translate_store(var);

		bytecode->addBranch(Instruction::BC_JA, lstart);
		bytecode->bind(lafter);
	}

	void bytecode_translator::visitWhileNode(mathvm::WhileNode *node) {
		Label lstart(bytecode);
		Label lafter(bytecode);

		bytecode->bind(lstart);
		node->whileExpr()->visit(this);
		resolve_int_unary(type_stack.top());
		type_stack.pop();

		bytecode->addInsn(Instruction::BC_ILOAD1);
		bytecode->addBranch(Instruction::BC_IFICMPNE, lafter);
		node->loopBlock()->visit(this);
		bytecode->addBranch(Instruction::BC_JA, lstart);
		bytecode->bind(lafter);
	}

	void bytecode_translator::visitReturnNode(mathvm::ReturnNode *node) {
		node->visitChildren(this);
		bytecode->addInsn(Instruction::BC_RETURN);
	}

	void bytecode_translator::visitFunctionNode(mathvm::FunctionNode *node) {
//		Scope* owner = node->body()->scope();
//		uint32_t params = node->parametersNumber();
//		for (uint32_t i = 0; i < params; ++i) {
//			AstVar* var = owner->lookupVariable(node->parameterName(params - i - 1));
//			translate_store(var);
//		}
		node->visitChildren(this);
	}

	void bytecode_translator::visitCallNode(mathvm::CallNode *node) {

		for (uint32_t i = 0; i < node->parametersNumber(); ++i) {
			node->parameterAt(node->parametersNumber() - i - 1)->visit(this);
		}
//		std::vector<VarType> tps;
//		while (!type_stack.empty()) {
//			tps.push_back(type_stack.top());
//			type_stack.pop();
//		}
//		std::cout << node->name() << ' ';
//		for (auto tp = tps.rbegin(); tp != tps.rend(); ++tp) {
//			std::cout << *tp << ' ';
//			type_stack.push(*tp);
//		}
//		std::cout << '\n';
		bytecode->addInsn(Instruction::BC_CALL);
		TranslatedFunction* tfunc = code->functionByName(node->name());
		bytecode->addUInt16(tfunc->id());
		for (uint16_t i = 0; i < tfunc->parametersNumber(); ++i)
			type_stack.pop();
		type_stack.push(tfunc->returnType());
	}

	void bytecode_translator::visitNativeCallNode(mathvm::NativeCallNode *node) {
	}

	void bytecode_translator::visitPrintNode(mathvm::PrintNode *node) {
		for (uint32_t i = 0; i < node->operands(); ++i) {
			node->operandAt(i)->visit(this);
			switch (type_stack.top()) {
				case VT_DOUBLE:
					bytecode->addInsn(Instruction::BC_DPRINT);
					break;
				case VT_INT:
					bytecode->addInsn(Instruction::BC_IPRINT);
					break;
				case VT_STRING:
					bytecode->addInsn(Instruction::BC_SPRINT);
					break;
				default:
					bytecode->addInsn(Instruction::BC_INVALID);
					break;
			}
			type_stack.pop();
		}
	}

	void bytecode_translator::first_to_double() {
		resolve_int_unary(type_stack.top());
		bytecode->addInsn(Instruction::BC_I2D);
		type_stack.pop();
		type_stack.push(VT_DOUBLE);
	}

	void bytecode_translator::second_to_double() {
		bytecode->addInsn(Instruction::BC_SWAP);
		first_to_double();
		bytecode->addInsn(Instruction::BC_SWAP);
	}

	VarType bytecode_translator::translate_binop(TokenKind kind) {
		VarType left = type_stack.top();
		type_stack.pop();
		VarType right = type_stack.top();
		type_stack.pop();
		switch (kind) {
			case tADD: {
				VarType expr_type = resolve_int_or_double_binary(left, right);
				bytecode->addInsn(expr_type == VT_INT ? Instruction::BC_IADD : Instruction::BC_DADD);
				return expr_type;
			}
			case tSUB: {
				VarType expr_type = resolve_int_or_double_binary(left, right);
				bytecode->addInsn(expr_type == VT_INT ? Instruction::BC_ISUB : Instruction::BC_DSUB);
				return expr_type;
			}
			case tMUL: {
				VarType expr_type = resolve_int_or_double_binary(left, right);
				bytecode->addInsn(expr_type == VT_INT ? Instruction::BC_IMUL : Instruction::BC_DMUL);
				return expr_type;
			}
			case tDIV: {
				VarType expr_type = resolve_int_or_double_binary(left, right);
				bytecode->addInsn(expr_type == VT_INT ? Instruction::BC_IDIV : Instruction::BC_DDIV);
				return expr_type;
			}
			case tMOD:
				bytecode->addInsn(Instruction::BC_IMOD);
				return resolve_int_binary(left, right);
			case tAND:
				bytecode->addInsn(Instruction::BC_IAAND);
				return resolve_int_binary(left, right);
			case tOR:
				bytecode->addInsn(Instruction::BC_IAOR);
				return resolve_int_binary(left, right);
			case tAAND:
				bytecode->addInsn(Instruction::BC_IAAND);
				return resolve_int_binary(left, right);
			case tAOR:
				bytecode->addInsn(Instruction::BC_IAOR);
				return resolve_int_binary(left, right);
			case tAXOR:
				bytecode->addInsn(Instruction::BC_IAXOR);
				return resolve_int_binary(left, right);
			case tNOT:
				bytecode->addInsn(Instruction::BC_INEG);
				return resolve_int_binary(left, right);
			case tLT:
			case tLE:
			case tGT:
			case tGE:
			case tEQ:
			case tNEQ:
				translate_cmp(kind, resolve_int_or_double_binary(left, right));
				return VT_INT;
			default:
				return VT_INVALID;
		}
		return VT_INVALID;
	}

	VarType bytecode_translator::translate_unop(TokenKind kind) {
		VarType toptype = type_stack.top();
		switch (kind) {
			case tADD:
				return resolve_int_or_double_unary(toptype);
			case tSUB:
				bytecode->addInsn((toptype == VT_DOUBLE) ? Instruction::BC_DNEG : Instruction::BC_INEG);
				return resolve_int_or_double_unary(toptype);
			case tNOT:
				bytecode->addInsn(Instruction::BC_ILOAD1);
				bytecode->addInsn(Instruction::BC_IAXOR);
				return resolve_int_unary(toptype);
			default:
				return VT_INVALID;
		}
	}

	/*
	 * a == b => cmp(a, b) == 0
	 * a != b => cmp(a, b) != 0
	 * a < b => cmp(a, b) < 0
	 * a <= b => cmp(a, b) <= 0
	 * a > b => cmp(a, b) == 1
	 * a >= b => cmp(a, b) >= 0
	 *
	 * if (cmp is false) {
	 *     jmp lfalse
	 * }
	 * cmp-result = 1
	 * jmp lafter;
	 *
	 * lfalse:
	 *     cmp-result = 0
	 *
	 * lafter:
	 *     ...
	 */
	void bytecode_translator::translate_cmp(TokenKind cmp_kind, VarType cmp_type) {
		bytecode->addInsn((cmp_type == VT_DOUBLE) ? Instruction::BC_DCMP : Instruction::BC_ICMP);

		Label lafter(bytecode);
		Label lfalse(bytecode);

		bytecode->addInsn(Instruction::BC_ILOAD0);

		// upper == 0, lower == cmp(a, b)
		switch (cmp_kind) {
			case tEQ:
				bytecode->addBranch(Instruction::BC_IFICMPNE, lfalse);
				break;
			case tNEQ:
				bytecode->addBranch(Instruction::BC_IFICMPE, lfalse);
				break;
			case tLT:
				bytecode->addBranch(Instruction::BC_IFICMPLE, lfalse);
				break;
			case tLE:
				bytecode->addBranch(Instruction::BC_IFICMPL, lfalse);
				break;
			case tGT:
				bytecode->addBranch(Instruction::BC_IFICMPGE, lfalse);
				break;
			case tGE:
				bytecode->addBranch(Instruction::BC_IFICMPG, lfalse);
				break;
			default:
				bytecode->addInsn(Instruction::BC_INVALID);
		}

		bytecode->addInsn(Instruction::BC_ILOAD1);
		bytecode->addBranch(Instruction::BC_JA, lafter);
		bytecode->bind(lfalse);
		bytecode->addInsn(Instruction::BC_ILOAD0);
		bytecode->bind(lafter);
	}

	void bytecode_translator::translate_function(TranslatedFunctionWrapper* wrapper) {
		AstFunction* astFunction = wrapper->get_function();
		save_bytecode(wrapper->name() != "<top>");

		FunctionNode* node = astFunction->node();
		Scope* owner = node->body()->scope();
		uint32_t params = node->parametersNumber();
		for (uint32_t i = 0; i < params; ++i) {
			AstVar* var = owner->lookupVariable(node->parameterName(params - i - 1));
			translate_store(var);
		}
		node->body()->visit(this);
		flush_bytecode_to_function(wrapper);
	}

	void bytecode_translator::traverse_scope(Scope* scope) {
		scopes.push_back(scope);
		std::vector<elem_t> v;
		vars_values.push_back(v);
		Scope::VarIterator var_iterator(scope);
		std::vector<TranslatedFunctionWrapper*> decls;
		while (var_iterator.hasNext()) {
			AstVar* var = var_iterator.next();
			elem_t elem;
			vars_values[scopes.size() - 1].push_back(elem);
			vars[var] = new variable(scopes.size() - 1, vars_values[scopes.size() - 1].size() - 1);
//			std::cout << var->name() << " " << vars[var]->scope_id << " " << vars[var]->var_id << '\n';
		}
		Scope::FunctionIterator function_iterator(scope);
		while (function_iterator.hasNext()) {
			AstFunction* func = function_iterator.next();
			TranslatedFunctionWrapper* tfunc = new TranslatedFunctionWrapper(func);
			functions[tfunc] = new function(scopes.size() - 1, last_func_id++);
			decls.push_back(tfunc);
			code->addFunction(tfunc);
		}
		for (uint32_t i = 0; i < scope->childScopeNumber(); ++i) {
			traverse_scope(scope->childScopeAt(i));
		}
		for (uint32_t i = 0; i < decls.size(); ++i) {
			translate_function(decls[i]);
			if (decls[i]->name() == "<top>")
				top = decls[i];
		}
	}


	void bytecode_translator::translate_load(const mathvm::AstVar *var) {
//		int32_t local_param_id = try_find_local(var);
//		if (local_param_id < 0) {
			bytecode->addInsn(get_load_insn(var));

			const AstVar *_var = var;
			type_stack.push(var->type());

			bytecode->addUInt16(vars[_var]->scope_id);
			bytecode->addUInt16(vars[_var]->var_id);
//		} else {
//			bytecode->addInsn(get_local_load_insn(var));
//			bytecode->addUInt16(local_param_id);
//		}
	}

	void bytecode_translator::translate_store(const mathvm::AstVar *var) {
//		int32_t local_param_id = try_find_local(var);
//		if (local_param_id > -10) {
			bytecode->addInsn(get_store_insn(var));

			bytecode->addUInt16(vars[var]->scope_id);
			bytecode->addUInt16(vars[var]->var_id);
//		} else {
//			bytecode->addInsn(get_local_store_insn(var));
//			bytecode->addUInt16(local_param_id);
//		}
	}

	Instruction bytecode_translator::get_load_insn(const AstVar* var) {

		switch (var->type()) {
			case VT_INT:
				return Instruction::BC_LOADCTXIVAR;
			case VT_DOUBLE:
				return Instruction::BC_LOADCTXDVAR;
			case VT_STRING:
				return Instruction::BC_LOADCTXSVAR;
			default:
				return Instruction::BC_INVALID;
		}
	}

	Instruction bytecode_translator::get_store_insn(const AstVar* var) {
		switch (var->type()) {
			case VT_INT:
				return Instruction::BC_STORECTXIVAR;
			case VT_DOUBLE:
				return Instruction::BC_STORECTXDVAR;
			case VT_STRING:
				return Instruction::BC_STORECTXSVAR;
			default:
				return Instruction::BC_INVALID;
		}
	}

	Instruction bytecode_translator::get_local_load_insn(const AstVar* var) {

		switch (var->type()) {
			case VT_INT:
				return Instruction::BC_LOADIVAR;
			case VT_DOUBLE:
				return Instruction::BC_LOADDVAR;
			case VT_STRING:
				return Instruction::BC_LOADSVAR;
			default:
				return Instruction::BC_INVALID;
		}
	}

	Instruction bytecode_translator::get_local_store_insn(const AstVar* var) {
		switch (var->type()) {
			case VT_INT:
				return Instruction::BC_STOREIVAR;
			case VT_DOUBLE:
				return Instruction::BC_STOREDVAR;
			case VT_STRING:
				return Instruction::BC_STORESVAR;
			default:
				return Instruction::BC_INVALID;
		}
	}

	void bytecode_translator::print_bytecode() const {
		uint16_t id = 0;
		TranslatedFunctionWrapper* function = dynamic_cast<TranslatedFunctionWrapper*>(code->functionById(id));
		while (function != 0) {
			std::cout << function->name() << ":\n";
			function->get_bytecode()->dump(std::cout);
			std::cout << "LOCALS: " << function->localsNumber() << "\n";
			std::cout << '\n';
			function = dynamic_cast<TranslatedFunctionWrapper*>(code->functionById(++id));
		}
	}

	Code* bytecode_translator::get_code() const {
		return code;
	}

	Bytecode* bytecode_translator::get_bytecode() const {
		return top->get_bytecode();
	}

	std::map<const AstVar*, variable*> bytecode_translator::get_vars() const {
		return vars;
	}

	std::vector< std::vector<elem_t> > bytecode_translator::get_vars_values() const {
		return vars_values;
	}

	elem_t bytecode_translator::get_var0() const {
		return var0;
	}

	elem_t bytecode_translator::get_var1() const {
		return var1;
	}

	elem_t bytecode_translator::get_var2() const {
		return var2;
	}

	elem_t bytecode_translator::get_var3() const {
		return var3;
	}

	Bytecode* copy(Bytecode* orig_bytecode) {
		Bytecode* bc = new Bytecode();
		for (uint32_t i = 0; i < orig_bytecode->length(); ++i) {
			bc->add(orig_bytecode->get(i));
		}
		return bc;
	}

	void bytecode_translator::save_bytecode(bool create_new_bytecode) {
		nested_functions_bytecodes.push_back(copy(bytecode));
		bytecode = new Bytecode();
	}

	void bytecode_translator::flush_bytecode_to_function(mathvm::TranslatedFunctionWrapper *wrapper) {
		wrapper->set_bytecode(copy(bytecode));
		bytecode = nested_functions_bytecodes.back();
		nested_functions_bytecodes.pop_back();
	}

	VarType bytecode_translator::resolve_int_or_double_binary(VarType left, VarType right) {
		int ints = 0, doubles = 0;
		if (left == VT_INT)
			++ints;
		else if (left == VT_DOUBLE) {
			++doubles;
		}
		if (right == VT_INT)
			++ints;
		else if (right == VT_DOUBLE) {
			++doubles;
		}
		if (ints == 2)
			return VT_INT;
		else if (ints + doubles == 2)
			return VT_DOUBLE;
		else {
			throw std::invalid_argument("required int or double");
		}
	}

	VarType bytecode_translator::resolve_int_binary(VarType left, VarType right) {
		if (left == VT_INT && right == VT_INT)
			return VT_INT;
		throw std::invalid_argument("required int");
	}

	VarType bytecode_translator::resolve_int_or_double_unary(VarType top_type) {
		if (top_type == VT_INT || top_type == VT_DOUBLE)
			return top_type;
		throw std::invalid_argument("required int or double");
	}

	VarType bytecode_translator::resolve_int_unary(VarType top_type) {
		if (top_type == VT_INT)
			return top_type;
		throw std::invalid_argument("required int or double");
	}

	int32_t bytecode_translator::try_find_local(const AstVar *param) {
		std::cout << functions_declarations_stack.size() << std::endl;
		for (auto it = functions_declarations_stack.rbegin(); it != functions_declarations_stack.rend(); ++it) {
			if ((*it)->contains(param)) {
				return (*it)->get_param(param)->second;
			}
		}
		return -1;
	}
}