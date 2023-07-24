#include "tilde.hpp"


gb_global Slice<TB_Arena *> global_tb_arenas;

gb_internal TB_Arena *cg_arena(void) {
	return global_tb_arenas[current_thread_index()];
}

// returns TB_TYPE_VOID if not trivially possible
gb_internal TB_DataType cg_data_type(Type *t) {
	GB_ASSERT(t != nullptr);
	t = core_type(t);
	i64 sz = type_size_of(t);
	switch (t->kind) {
	case Type_Basic:
		switch (t->Basic.kind) {
		case Basic_bool:
		case Basic_b8:
		case Basic_b16:
		case Basic_b32:
		case Basic_b64:

		case Basic_i8:
		case Basic_u8:
		case Basic_i16:
		case Basic_u16:
		case Basic_i32:
		case Basic_u32:
		case Basic_i64:
		case Basic_u64:
		case Basic_i128:
		case Basic_u128:

		case Basic_rune:

		case Basic_int:
		case Basic_uint:
		case Basic_uintptr:
		case Basic_typeid:
			return TB_TYPE_INTN(cast(u16)gb_min(8*sz, 64));

		case Basic_f16: return TB_TYPE_F16;
		case Basic_f32: return TB_TYPE_F32;
		case Basic_f64: return TB_TYPE_F64;

		case Basic_rawptr:  return TB_TYPE_PTR;
		case Basic_cstring: return TB_TYPE_PTR;


		// Endian Specific Types
		case Basic_i16le:
		case Basic_u16le:
		case Basic_i32le:
		case Basic_u32le:
		case Basic_i64le:
		case Basic_u64le:
		case Basic_i128le:
		case Basic_u128le:
		case Basic_i16be:
		case Basic_u16be:
		case Basic_i32be:
		case Basic_u32be:
		case Basic_i64be:
		case Basic_u64be:
		case Basic_i128be:
		case Basic_u128be:
			return TB_TYPE_INTN(cast(u16)gb_min(8*sz, 64));

		case Basic_f16le: return TB_TYPE_F16;
		case Basic_f32le: return TB_TYPE_F32;
		case Basic_f64le: return TB_TYPE_F64;

		case Basic_f16be: return TB_TYPE_F16;
		case Basic_f32be: return TB_TYPE_F32;
		case Basic_f64be: return TB_TYPE_F64;
		}
		break;

	case Type_Pointer:
	case Type_MultiPointer:
	case Type_Proc:
		return TB_TYPE_PTR;

	case Type_BitSet:
		return cg_data_type(bit_set_to_int(t));

	case Type_RelativePointer:
		return cg_data_type(t->RelativePointer.base_integer);
	}

	// unknown
	return {};
}


gb_internal cgValue cg_value(TB_Global *g, Type *type) {
	return cg_value((TB_Symbol *)g, type);
}
gb_internal cgValue cg_value(TB_External *e, Type *type) {
	return cg_value((TB_Symbol *)e, type);
}
gb_internal cgValue cg_value(TB_Function *f, Type *type) {
	return cg_value((TB_Symbol *)f, type);
}
gb_internal cgValue cg_value(TB_Symbol *s, Type *type) {
	cgValue v = {};
	v.kind = cgValue_Symbol;
	v.type = type;
	v.symbol = s;
	return v;
}
gb_internal cgValue cg_value(TB_Node *node, Type *type) {
	cgValue v = {};
	v.kind = cgValue_Value;
	v.type = type;
	v.node = node;
	return v;
}
gb_internal cgValue cg_lvalue_addr(TB_Node *node, Type *type) {
	GB_ASSERT(node->dt.type == TB_PTR);
	cgValue v = {};
	v.kind = cgValue_Addr;
	v.type = type;
	v.node = node;
	return v;
}

gb_internal cgValue cg_lvalue_addr_to_value(cgValue v) {
	if (v.kind == cgValue_Value) {
		GB_ASSERT(is_type_pointer(v.type));
		GB_ASSERT(v.node->dt.type == TB_PTR);
	} else {
		GB_ASSERT(v.kind == cgValue_Addr);
		GB_ASSERT(v.node->dt.type == TB_PTR);
		v.kind = cgValue_Value;
		v.type = alloc_type_pointer(v.type);
	}
	return v;
}

gb_internal cgValue cg_value_multi(cgValueMulti *multi, Type *type) {
	GB_ASSERT(type->kind == Type_Tuple);
	GB_ASSERT(multi != nullptr);
	GB_ASSERT(type->Tuple.variables.count > 1);
	GB_ASSERT(multi->values.count == type->Tuple.variables.count);
	cgValue v = {};
	v.kind = cgValue_Multi;
	v.type = type;
	v.multi = multi;
	return v;
}

gb_internal cgValue cg_value_multi(Slice<cgValue> const &values, Type *type) {
	cgValueMulti *multi = gb_alloc_item(permanent_allocator(), cgValueMulti);
	multi->values = values;
	return cg_value_multi(multi, type);
}


gb_internal cgValue cg_value_multi2(cgValue const &x, cgValue const &y, Type *type) {
	GB_ASSERT(type->kind == Type_Tuple);
	GB_ASSERT(type->Tuple.variables.count == 2);
	cgValueMulti *multi = gb_alloc_item(permanent_allocator(), cgValueMulti);
	multi->values = slice_make<cgValue>(permanent_allocator(), 2);
	multi->values[0] = x;
	multi->values[1] = y;
	return cg_value_multi(multi, type);
}


gb_internal cgAddr cg_addr(cgValue const &value) {
	GB_ASSERT(value.kind != cgValue_Multi);
	cgAddr addr = {};
	addr.kind = cgAddr_Default;
	addr.addr = value;
	if (addr.addr.kind == cgValue_Addr) {
		GB_ASSERT(addr.addr.node != nullptr);
		addr.addr.kind = cgValue_Value;
		addr.addr.type = alloc_type_pointer(addr.addr.type);
	}
	return addr;
}

gb_internal void cg_set_debug_pos_from_node(cgProcedure *p, Ast *node) {
	if (node) {
		TokenPos pos = ast_token(node).pos;
		TB_FileID *file_id = map_get(&p->module->file_id_map, cast(uintptr)pos.file_id);
		if (file_id) {
			tb_inst_set_location(p->func, *file_id, pos.line);
		}
	}
}


gb_internal void cg_add_entity(cgModule *m, Entity *e, cgValue const &val) {
	if (e) {
		rw_mutex_lock(&m->values_mutex);
		GB_ASSERT(val.node != nullptr);
		map_set(&m->values, e, val);
		rw_mutex_unlock(&m->values_mutex);
	}
}

gb_internal void cg_add_member(cgModule *m, String const &name, cgValue const &val) {
	if (name.len > 0) {
		rw_mutex_lock(&m->values_mutex);
		string_map_set(&m->members, name, val);
		rw_mutex_unlock(&m->values_mutex);
	}
}

gb_internal void cg_add_procedure_value(cgModule *m, cgProcedure *p) {
	rw_mutex_lock(&m->values_mutex);
	if (p->entity != nullptr) {
		map_set(&m->procedure_values, p->func, p->entity);
	}
	string_map_set(&m->procedures, p->name, p);
	rw_mutex_unlock(&m->values_mutex);

}

gb_internal isize cg_type_info_index(CheckerInfo *info, Type *type, bool err_on_not_found=true) {
	auto *set = &info->minimum_dependency_type_info_set;
	isize index = type_info_index(info, type, err_on_not_found);
	if (index >= 0) {
		auto *found = map_get(set, index);
		if (found) {
			GB_ASSERT(*found >= 0);
			return *found + 1;
		}
	}
	if (err_on_not_found) {
		GB_PANIC("NOT FOUND lb_type_info_index %s @ index %td", type_to_string(type), index);
	}
	return -1;
}


gb_internal u64 cg_typeid_as_u64(cgModule *m, Type *type) {
	GB_ASSERT(!build_context.no_rtti);

	type = default_type(type);

	u64 id = cast(u64)cg_type_info_index(m->info, type);
	GB_ASSERT(id >= 0);

	u64 kind = Typeid_Invalid;
	u64 named = is_type_named(type) && type->kind != Type_Basic;
	u64 special = 0;
	u64 reserved = 0;

	Type *bt = base_type(type);
	TypeKind tk = bt->kind;
	switch (tk) {
	case Type_Basic: {
		u32 flags = bt->Basic.flags;
		if (flags & BasicFlag_Boolean)  kind = Typeid_Boolean;
		if (flags & BasicFlag_Integer)  kind = Typeid_Integer;
		if (flags & BasicFlag_Unsigned) kind = Typeid_Integer;
		if (flags & BasicFlag_Float)    kind = Typeid_Float;
		if (flags & BasicFlag_Complex)  kind = Typeid_Complex;
		if (flags & BasicFlag_Pointer)  kind = Typeid_Pointer;
		if (flags & BasicFlag_String)   kind = Typeid_String;
		if (flags & BasicFlag_Rune)     kind = Typeid_Rune;
	} break;
	case Type_Pointer:         kind = Typeid_Pointer;          break;
	case Type_MultiPointer:    kind = Typeid_Multi_Pointer;    break;
	case Type_Array:           kind = Typeid_Array;            break;
	case Type_Matrix:          kind = Typeid_Matrix;           break;
	case Type_EnumeratedArray: kind = Typeid_Enumerated_Array; break;
	case Type_Slice:           kind = Typeid_Slice;            break;
	case Type_DynamicArray:    kind = Typeid_Dynamic_Array;    break;
	case Type_Map:             kind = Typeid_Map;              break;
	case Type_Struct:          kind = Typeid_Struct;           break;
	case Type_Enum:            kind = Typeid_Enum;             break;
	case Type_Union:           kind = Typeid_Union;            break;
	case Type_Tuple:           kind = Typeid_Tuple;            break;
	case Type_Proc:            kind = Typeid_Procedure;        break;
	case Type_BitSet:          kind = Typeid_Bit_Set;          break;
	case Type_SimdVector:      kind = Typeid_Simd_Vector;      break;
	case Type_RelativePointer: kind = Typeid_Relative_Pointer; break;
	case Type_RelativeSlice:   kind = Typeid_Relative_Slice;   break;
	case Type_SoaPointer:      kind = Typeid_SoaPointer;       break;
	}

	if (is_type_cstring(type)) {
		special = 1;
	} else if (is_type_integer(type) && !is_type_unsigned(type)) {
		special = 1;
	}

	u64 data = 0;
	if (build_context.ptr_size == 4) {
		GB_ASSERT(id <= (1u<<24u));
		data |= (id       &~ (1u<<24)) << 0u;  // index
		data |= (kind     &~ (1u<<5))  << 24u; // kind
		data |= (named    &~ (1u<<1))  << 29u; // named
		data |= (special  &~ (1u<<1))  << 30u; // special
		data |= (reserved &~ (1u<<1))  << 31u; // reserved
	} else {
		GB_ASSERT(build_context.ptr_size == 8);
		GB_ASSERT(id <= (1ull<<56u));
		data |= (id       &~ (1ull<<56)) << 0ul;  // index
		data |= (kind     &~ (1ull<<5))  << 56ull; // kind
		data |= (named    &~ (1ull<<1))  << 61ull; // named
		data |= (special  &~ (1ull<<1))  << 62ull; // special
		data |= (reserved &~ (1ull<<1))  << 63ull; // reserved
	}
	return data;
}

gb_internal cgValue cg_typeid(cgProcedure *p, Type *t) {
	u64 x = cg_typeid_as_u64(p->module, t);
	return cg_value(tb_inst_uint(p->func, cg_data_type(t_typeid), x), t_typeid);
}


struct cgGlobalVariable {
	cgValue var;
	cgValue init;
	DeclInfo *decl;
	bool is_initialized;
};

// Returns already_has_entry_point
gb_internal bool cg_global_variables_create(cgModule *m) {
	isize global_variable_max_count = 0;
	bool already_has_entry_point = false;

	for (Entity *e : m->info->entities) {
		String name = e->token.string;

		if (e->kind == Entity_Variable) {
			global_variable_max_count++;
		} else if (e->kind == Entity_Procedure) {
			if ((e->scope->flags&ScopeFlag_Init) && name == "main") {
				GB_ASSERT(e == m->info->entry_point);
			}
			if (build_context.command_kind == Command_test &&
			    (e->Procedure.is_export || e->Procedure.link_name.len > 0)) {
				String link_name = e->Procedure.link_name;
				if (e->pkg->kind == Package_Runtime) {
					if (link_name == "main"           ||
					    link_name == "DllMain"        ||
					    link_name == "WinMain"        ||
					    link_name == "wWinMain"       ||
					    link_name == "mainCRTStartup" ||
					    link_name == "_start") {
						already_has_entry_point = true;
					}
				}
			}
		}
	}
	auto global_variables = array_make<cgGlobalVariable>(permanent_allocator(), 0, global_variable_max_count);

	auto *min_dep_set = &m->info->minimum_dependency_set;

	for (DeclInfo *d : m->info->variable_init_order) {
		Entity *e = d->entity;

		if ((e->scope->flags & ScopeFlag_File) == 0) {
			continue;
		}

		if (!ptr_set_exists(min_dep_set, e)) {
			continue;
		}

		DeclInfo *decl = decl_info_of_entity(e);
		if (decl == nullptr) {
			continue;
		}
		GB_ASSERT(e->kind == Entity_Variable);

		bool is_foreign = e->Variable.is_foreign;
		bool is_export  = e->Variable.is_export;

		String name = cg_get_entity_name(m, e);

		TB_Linkage linkage = TB_LINKAGE_PRIVATE;

		if (is_foreign) {
			linkage = TB_LINKAGE_PUBLIC;
			// lb_add_foreign_library_path(m, e->Variable.foreign_library);
			// lb_set_wasm_import_attributes(g.value, e, name);
		} else if (is_export) {
			linkage = TB_LINKAGE_PUBLIC;
		}
		// lb_set_linkage_from_entity_flags(m, g.value, e->flags);

		TB_DebugType *debug_type = cg_debug_type(m, e->type);
		TB_Global *global = tb_global_create(m->mod, name.len, cast(char const *)name.text, debug_type, linkage);
		cgValue g = cg_value(global, alloc_type_pointer(e->type));

		TB_ModuleSection *section = tb_module_get_data(m->mod);

		if (e->Variable.thread_local_model != "") {
			section = tb_module_get_tls(m->mod);
		}
		if (e->Variable.link_section.len > 0) {
			// TODO(bill): custom module sections
			// LLVMSetSection(g.value, alloc_cstring(permanent_allocator(), e->Variable.link_section));
		}

		size_t max_objects = 0;
		tb_global_set_storage(m->mod, section, global, type_size_of(e->type), type_align_of(e->type), max_objects);

		cgGlobalVariable var = {};
		var.var = g;
		var.decl = decl;

		if (decl->init_expr != nullptr) {
			// TypeAndValue tav = type_and_value_of_expr(decl->init_expr);
			// if (!is_type_any(e->type) && !is_type_union(e->type)) {
			// 	if (tav.mode != Addressing_Invalid) {
			// 		if (tav.value.kind != ExactValue_Invalid) {
			// 			ExactValue v = tav.value;
			// 			lbValue init = lb_const_value(m, tav.type, v);
			// 			LLVMSetInitializer(g.value, init.value);
			// 			var.is_initialized = true;
			// 		}
			// 	}
			// }
			// if (!var.is_initialized && is_type_untyped_nil(tav.type)) {
			// 	var.is_initialized = true;
			// }
		}

		array_add(&global_variables, var);

		cg_add_entity(m, e, g);
		cg_add_member(m, name, g);
	}



	if (build_context.no_rtti) {
		return already_has_entry_point;
	}

	CheckerInfo *info = m->info;
	{ // Add type info data
		isize max_type_info_count = info->minimum_dependency_type_info_set.count+1;
		// gb_printf_err("max_type_info_count: %td\n", max_type_info_count);
		Type *t = alloc_type_array(t_type_info, max_type_info_count);

		TB_Global *g = tb_global_create(m->mod, -1, CG_TYPE_INFO_DATA_NAME, nullptr, TB_LINKAGE_PRIVATE);
		tb_global_set_storage(m->mod, tb_module_get_rdata(m->mod), g, type_size_of(t), 16, max_type_info_count);

		cgValue value = cg_value(g, alloc_type_pointer(t));
		cg_global_type_info_data_entity = alloc_entity_variable(nullptr, make_token_ident(CG_TYPE_INFO_DATA_NAME), t, EntityState_Resolved);
		cg_add_entity(m, cg_global_type_info_data_entity, value);
	}

	{ // Type info member buffer
		// NOTE(bill): Removes need for heap allocation by making it global memory
		isize count = 0;

		for (Type *t : m->info->type_info_types) {
			isize index = cg_type_info_index(m->info, t, false);
			if (index < 0) {
				continue;
			}

			switch (t->kind) {
			case Type_Union:
				count += t->Union.variants.count;
				break;
			case Type_Struct:
				count += t->Struct.fields.count;
				break;
			case Type_Tuple:
				count += t->Tuple.variables.count;
				break;
			}
		}

		if (count > 0) {
			{
				char const *name = CG_TYPE_INFO_TYPES_NAME;
				Type *t = alloc_type_array(t_type_info_ptr, count);
				TB_Global *g = tb_global_create(m->mod, -1, name, nullptr, TB_LINKAGE_PRIVATE);
				tb_global_set_storage(m->mod, tb_module_get_rdata(m->mod), g, type_size_of(t), 16, count);
				cg_global_type_info_member_types = cg_addr(cg_value(g, alloc_type_pointer(t)));

			}
			{
				char const *name = CG_TYPE_INFO_NAMES_NAME;
				Type *t = alloc_type_array(t_string, count);
				TB_Global *g = tb_global_create(m->mod, -1, name, nullptr, TB_LINKAGE_PRIVATE);
				tb_global_set_storage(m->mod, tb_module_get_rdata(m->mod), g, type_size_of(t), 16, count);
				cg_global_type_info_member_names = cg_addr(cg_value(g, alloc_type_pointer(t)));
			}
			{
				char const *name = CG_TYPE_INFO_OFFSETS_NAME;
				Type *t = alloc_type_array(t_uintptr, count);
				TB_Global *g = tb_global_create(m->mod, -1, name, nullptr, TB_LINKAGE_PRIVATE);
				tb_global_set_storage(m->mod, tb_module_get_rdata(m->mod), g, type_size_of(t), 16, count);
				cg_global_type_info_member_offsets = cg_addr(cg_value(g, alloc_type_pointer(t)));
			}

			{
				char const *name = CG_TYPE_INFO_USINGS_NAME;
				Type *t = alloc_type_array(t_bool, count);
				TB_Global *g = tb_global_create(m->mod, -1, name, nullptr, TB_LINKAGE_PRIVATE);
				tb_global_set_storage(m->mod, tb_module_get_rdata(m->mod), g, type_size_of(t), 16, count);
				cg_global_type_info_member_usings = cg_addr(cg_value(g, alloc_type_pointer(t)));
			}

			{
				char const *name = CG_TYPE_INFO_TAGS_NAME;
				Type *t = alloc_type_array(t_string, count);
				TB_Global *g = tb_global_create(m->mod, -1, name, nullptr, TB_LINKAGE_PRIVATE);
				tb_global_set_storage(m->mod, tb_module_get_rdata(m->mod), g, type_size_of(t), 16, count);
				cg_global_type_info_member_tags = cg_addr(cg_value(g, alloc_type_pointer(t)));
			}
		}
	}

	return already_has_entry_point;
}

gb_internal cgModule *cg_module_create(Checker *c) {
	cgModule *m = gb_alloc_item(permanent_allocator(), cgModule);

	m->checker = c;
	m->info = &c->info;


	TB_FeatureSet feature_set = {};
	bool is_jit = false;
	m->mod = tb_module_create(TB_ARCH_X86_64, TB_SYSTEM_WINDOWS, &feature_set, is_jit);
	tb_module_set_tls_index(m->mod, 10, "_tls_index");

	map_init(&m->values);
	array_init(&m->procedures_to_generate, heap_allocator());

	map_init(&m->file_id_map);

	map_init(&m->debug_type_map);
	map_init(&m->proc_debug_type_map);
	map_init(&m->proc_proto_map);


	for_array(id, global_files) {
		if (AstFile *f = global_files[id]) {
			char const *path = alloc_cstring(permanent_allocator(), f->fullpath);
			map_set(&m->file_id_map, cast(uintptr)id, tb_file_create(m->mod, path));
		}
	}

	return m;
}

gb_internal void cg_module_destroy(cgModule *m) {
	map_destroy(&m->values);
	array_free(&m->procedures_to_generate);
	map_destroy(&m->file_id_map);
	map_destroy(&m->debug_type_map);
	map_destroy(&m->proc_debug_type_map);
	map_destroy(&m->proc_proto_map);

	tb_module_destroy(m->mod);
}

gb_internal String cg_set_nested_type_name_ir_mangled_name(Entity *e, cgProcedure *p) {
	// NOTE(bill, 2020-03-08): A polymorphic procedure may take a nested type declaration
	// and as a result, the declaration does not have time to determine what it should be

	GB_ASSERT(e != nullptr && e->kind == Entity_TypeName);
	if (e->TypeName.ir_mangled_name.len != 0)  {
		return e->TypeName.ir_mangled_name;
	}
	GB_ASSERT((e->scope->flags & ScopeFlag_File) == 0);

	if (p == nullptr) {
		Entity *proc = nullptr;
		if (e->parent_proc_decl != nullptr) {
			proc = e->parent_proc_decl->entity;
		} else {
			Scope *scope = e->scope;
			while (scope != nullptr && (scope->flags & ScopeFlag_Proc) == 0) {
				scope = scope->parent;
			}
			GB_ASSERT(scope != nullptr);
			GB_ASSERT(scope->flags & ScopeFlag_Proc);
			proc = scope->procedure_entity;
		}
		GB_ASSERT(proc->kind == Entity_Procedure);
		if (proc->cg_procedure != nullptr) {
			p = proc->cg_procedure;
		}
	}

	// NOTE(bill): Generate a new name
	// parent_proc.name-guid
	String ts_name = e->token.string;

	if (p != nullptr) {
		isize name_len = p->name.len + 1 + ts_name.len + 1 + 10 + 1;
		char *name_text = gb_alloc_array(permanent_allocator(), char, name_len);
		u32 guid = 1+p->module->nested_type_name_guid.fetch_add(1);
		name_len = gb_snprintf(name_text, name_len, "%.*s" ABI_PKG_NAME_SEPARATOR "%.*s-%u", LIT(p->name), LIT(ts_name), guid);

		String name = make_string(cast(u8 *)name_text, name_len-1);
		e->TypeName.ir_mangled_name = name;
		return name;
	} else {
		// NOTE(bill): a nested type be required before its parameter procedure exists. Just give it a temp name for now
		isize name_len = 9 + 1 + ts_name.len + 1 + 10 + 1;
		char *name_text = gb_alloc_array(permanent_allocator(), char, name_len);
		static std::atomic<u32> guid;
		name_len = gb_snprintf(name_text, name_len, "_internal" ABI_PKG_NAME_SEPARATOR "%.*s-%u", LIT(ts_name), 1+guid.fetch_add(1));

		String name = make_string(cast(u8 *)name_text, name_len-1);
		e->TypeName.ir_mangled_name = name;
		return name;
	}
}

gb_internal String cg_mangle_name(cgModule *m, Entity *e) {
	String name = e->token.string;

	AstPackage *pkg = e->pkg;
	GB_ASSERT_MSG(pkg != nullptr, "Missing package for '%.*s'", LIT(name));
	String pkgn = pkg->name;
	GB_ASSERT(!rune_is_digit(pkgn[0]));
	if (pkgn == "llvm") {
		GB_PANIC("llvm. entities are not allowed with the tilde backend");
	}

	isize max_len = pkgn.len + 1 + name.len + 1;
	bool require_suffix_id = is_type_polymorphic(e->type, true);

	if ((e->scope->flags & (ScopeFlag_File | ScopeFlag_Pkg)) == 0) {
		require_suffix_id = true;
	} else if (is_blank_ident(e->token)) {
		require_suffix_id = true;
	}if (e->flags & EntityFlag_NotExported) {
		require_suffix_id = true;
	}

	if (require_suffix_id) {
		max_len += 21;
	}

	char *new_name = gb_alloc_array(permanent_allocator(), char, max_len);
	isize new_name_len = gb_snprintf(
		new_name, max_len,
		"%.*s" ABI_PKG_NAME_SEPARATOR "%.*s", LIT(pkgn), LIT(name)
	);
	if (require_suffix_id) {
		char *str = new_name + new_name_len-1;
		isize len = max_len-new_name_len;
		isize extra = gb_snprintf(str, len, "-%llu", cast(unsigned long long)e->id);
		new_name_len += extra-1;
	}

	String mangled_name = make_string((u8 const *)new_name, new_name_len-1);
	return mangled_name;
}

gb_internal String cg_get_entity_name(cgModule *m, Entity *e) {
	if (e != nullptr && e->kind == Entity_TypeName && e->TypeName.ir_mangled_name.len != 0) {
		return e->TypeName.ir_mangled_name;
	}
	GB_ASSERT(e != nullptr);

	if (e->pkg == nullptr) {
		return e->token.string;
	}

	if (e->kind == Entity_TypeName && (e->scope->flags & ScopeFlag_File) == 0) {
		return cg_set_nested_type_name_ir_mangled_name(e, nullptr);
	}

	String name = {};

	bool no_name_mangle = false;

	if (e->kind == Entity_Variable) {
		bool is_foreign = e->Variable.is_foreign;
		bool is_export  = e->Variable.is_export;
		no_name_mangle = e->Variable.link_name.len > 0 || is_foreign || is_export;
		if (e->Variable.link_name.len > 0) {
			return e->Variable.link_name;
		}
	} else if (e->kind == Entity_Procedure && e->Procedure.link_name.len > 0) {
		return e->Procedure.link_name;
	} else if (e->kind == Entity_Procedure && e->Procedure.is_export) {
		no_name_mangle = true;
	}

	if (!no_name_mangle) {
		name = cg_mangle_name(m, e);
	}
	if (name.len == 0) {
		name = e->token.string;
	}

	if (e->kind == Entity_TypeName) {
		e->TypeName.ir_mangled_name = name;
	} else if (e->kind == Entity_Procedure) {
		e->Procedure.link_name = name;
	}

	return name;
}

#include "tilde_const.cpp"
#include "tilde_debug.cpp"
#include "tilde_expr.cpp"
#include "tilde_builtin.cpp"
#include "tilde_proc.cpp"
#include "tilde_stmt.cpp"


gb_internal String cg_filepath_obj_for_module(cgModule *m) {
	String path = concatenate3_strings(permanent_allocator(),
		build_context.build_paths[BuildPath_Output].basename,
		STR_LIT("/"),
		build_context.build_paths[BuildPath_Output].name
	);

	// if (m->file) {
	// 	char buf[32] = {};
	// 	isize n = gb_snprintf(buf, gb_size_of(buf), "-%u", m->file->id);
	// 	String suffix = make_string((u8 *)buf, n-1);
	// 	path = concatenate_strings(permanent_allocator(), path, suffix);
	// } else if (m->pkg) {
	// 	path = concatenate3_strings(permanent_allocator(), path, STR_LIT("-"), m->pkg->name);
	// }

	String ext = {};

	if (build_context.build_mode == BuildMode_Assembly) {
		ext = STR_LIT(".S");
	} else {
		if (is_arch_wasm()) {
			ext = STR_LIT(".wasm.o");
		} else {
			switch (build_context.metrics.os) {
			case TargetOs_windows:
				ext = STR_LIT(".obj");
				break;
			default:
			case TargetOs_darwin:
			case TargetOs_linux:
			case TargetOs_essence:
				ext = STR_LIT(".o");
				break;

			case TargetOs_freestanding:
				switch (build_context.metrics.abi) {
				default:
				case TargetABI_Default:
				case TargetABI_SysV:
					ext = STR_LIT(".o");
					break;
				case TargetABI_Win64:
					ext = STR_LIT(".obj");
					break;
				}
				break;
			}
		}
	}

	return concatenate_strings(permanent_allocator(), path, ext);
}


gb_internal bool cg_generate_code(Checker *c, LinkerData *linker_data) {
	TIME_SECTION("Tilde Module Initializtion");

	CheckerInfo *info = &c->info;

	linker_data_init(linker_data, info, c->parser->init_fullpath);

	global_tb_arenas = slice_make<TB_Arena *>(permanent_allocator(), global_thread_pool.threads.count);
	for_array(i, global_tb_arenas) {
		global_tb_arenas[i] = tb_default_arena();
	}

	cgModule *m = cg_module_create(c);
	defer (cg_module_destroy(m));

	TIME_SECTION("Tilde Global Variables");

	bool already_has_entry_point = cg_global_variables_create(m);
	gb_unused(already_has_entry_point);

	if (true) {
		Type *proc_type = alloc_type_proc(nullptr, nullptr, 0, nullptr, 0, false, ProcCC_Odin);
		cgProcedure *p = cg_procedure_create_dummy(m, str_lit(CG_STARTUP_RUNTIME_PROC_NAME), proc_type);
		p->is_startup = true;

		cg_procedure_begin(p);
		cg_procedure_end(p);
	}

	if (true) {
		Type *proc_type = alloc_type_proc(nullptr, nullptr, 0, nullptr, 0, false, ProcCC_Odin);
		cgProcedure *p = cg_procedure_create_dummy(m, str_lit(CG_CLEANUP_RUNTIME_PROC_NAME), proc_type);
		p->is_startup = true;

		cg_procedure_begin(p);
		cg_procedure_end(p);
	}

	auto *min_dep_set = &info->minimum_dependency_set;

	for (Entity *e : info->entities) {
		String  name  = e->token.string;
		Scope * scope = e->scope;

		if ((scope->flags & ScopeFlag_File) == 0) {
			continue;
		}

		Scope *package_scope = scope->parent;
		GB_ASSERT(package_scope->flags & ScopeFlag_Pkg);

		if (e->kind != Entity_Procedure) {
			continue;
		}

		if (!ptr_set_exists(min_dep_set, e)) {
			// NOTE(bill): Nothing depends upon it so doesn't need to be built
			continue;
		}
		if (cgProcedure *p = cg_procedure_create(m, e)) {
			array_add(&m->procedures_to_generate, p);
		}
	}


	for (isize i = 0; i < m->procedures_to_generate.count; i++) {
		cg_procedure_generate(m->procedures_to_generate[i]);
	}

	TB_DebugFormat debug_format = TB_DEBUGFMT_NONE;
	if (build_context.ODIN_DEBUG || true) {
		switch (build_context.metrics.os) {
		case TargetOs_windows:
			debug_format = TB_DEBUGFMT_CODEVIEW;
			break;
		case TargetOs_darwin:
		case TargetOs_linux:
		case TargetOs_essence:
		case TargetOs_freebsd:
		case TargetOs_openbsd:
			debug_format = TB_DEBUGFMT_DWARF;
			break;
		}
	}
	TB_ExportBuffer export_buffer = tb_module_object_export(m->mod, debug_format);
	defer (tb_export_buffer_free(export_buffer));

	String filepath_obj = cg_filepath_obj_for_module(m);
	array_add(&linker_data->output_object_paths, filepath_obj);
	GB_ASSERT(tb_export_buffer_to_file(export_buffer, cast(char const *)filepath_obj.text));

	return true;
}

#undef ABI_PKG_NAME_SEPARATOR