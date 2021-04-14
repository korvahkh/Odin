package odin_printer

import "core:odin/ast"
import "core:odin/tokenizer"
import "core:strings"
import "core:runtime"
import "core:fmt"
import "core:unicode/utf8"
import "core:mem"

Line_Type_Enum :: enum{Line_Comment, Value_Decl, Switch_Stmt, Struct};

Line_Type :: bit_set[Line_Type_Enum];

Line :: struct {
    format_tokens: [dynamic] Format_Token,
    finalized: bool,
    used: bool,
	depth: int,
	types: Line_Type, //for performance, so you don't have to verify what types are in it by going through the tokens - might give problems when adding linebreaking
}

Format_Token :: struct {
    kind: tokenizer.Token_Kind,
    text: string,
    spaces_before: int,
	parameter_count: int,
}

Printer :: struct {
	string_builder:       strings.Builder,
	config:               Config,
	depth:                int, //the identation depth
	comments:             [dynamic]^ast.Comment_Group,
	latest_comment_index: int,
	allocator:            mem.Allocator,
	file:                 ^ast.File,
    source_position:      tokenizer.Pos,
	last_source_position: tokenizer.Pos,
    lines:                [dynamic] Line, //need to look into a better data structure, one that can handle inserting lines rather than appending
    skip_semicolon:       bool,
	current_line:         ^Line,
	current_line_index:   int,
	last_line_index:      int,
	last_token:           ^Format_Token,
	merge_next_token:     bool,
	space_next_token:     bool,
	debug:                bool,
}

Config :: struct {
	spaces:               int, //Spaces per indentation
	newline_limit:        int, //The limit of newlines between statements and declarations.
	tabs:                 bool, //Enable or disable tabs
	convert_do:           bool, //Convert all do statements to brace blocks
	semicolons:           bool, //Enable semicolons
	split_multiple_stmts: bool,
	align_switch:         bool,
	brace_style:          Brace_Style,
	align_assignments:    bool,
	align_structs:        bool,
	align_style:          Alignment_Style,
	indent_cases:         bool,
	newline_style:        Newline_Style,
}

Brace_Style :: enum {
	_1TBS,
	Allman,
	Stroustrup,
	K_And_R,
}

Block_Type :: enum {
	None,
	If_Stmt,
	Proc,
	Generic,
	Comp_Lit,
	Switch_Stmt,
}

Alignment_Style :: enum {
	Align_On_Colon_And_Equals,
	Align_On_Type_And_Equals,
}

Newline_Style :: enum {
	CRLF,
	LF,
}

default_style := Config {
	spaces = 4,
	newline_limit = 2,
	convert_do = false,
	semicolons = true,
	tabs = false,
	brace_style = ._1TBS,
	split_multiple_stmts = true,
	align_assignments = true,
	align_style = .Align_On_Type_And_Equals,
	indent_cases = false,
	align_switch = true,
	align_structs = true,
	newline_style = .LF,
};

make_printer :: proc(config: Config, allocator := context.allocator) -> Printer {
	return {
		config = config,
		allocator = allocator,
		debug = false,
	};
}

print :: proc(p: ^Printer, file: ^ast.File) -> string {

	p.comments = file.comments;
	
	if len(file.decls) > 0 {
		p.lines = make([dynamic] Line, 0, (file.decls[len(file.decls)-1].end.line - file.decls[0].pos.line) * 2, context.temp_allocator);
	}

	set_line(p, 0);

	push_generic_token(p, .Package, 0);
	push_ident_token(p, file.pkg_name, 1);

    for decl in file.decls {
        visit_decl(p, cast(^ast.Decl)decl);
    }

	if len(p.comments) > 0 {
		infinite := p.comments[len(p.comments)-1].end;
		infinite.offset = 9999999;
		push_comments(p, infinite);
	}

	fix_lines(p);

    builder := strings.make_builder(0, mem.megabytes(5), p.allocator);

    last_line := 0;

	newline: string;

	if p.config.newline_style == .LF {
		newline = "\n";
	} else {
		newline = "\r\n";
	}

    for line, line_index in p.lines {
        diff_line := line_index - last_line;

		for i := 0; i < diff_line; i += 1 {
			strings.write_string(&builder, newline);
		}
		
		if p.config.tabs {
			for i := 0; i < line.depth; i += 1 {
				strings.write_byte(&builder, '\t');
			}
		} else {
			for i := 0; i < line.depth * p.config.spaces; i += 1 {
				strings.write_byte(&builder, ' ');
			}
		}

		if p.debug {
			strings.write_string(&builder, fmt.tprintf("line %v: ", line_index));
		}

		for format_token in line.format_tokens {

			for i := 0; i < format_token.spaces_before; i += 1 {
				strings.write_byte(&builder, ' ');
			}

			strings.write_string(&builder, format_token.text);
		}
    
		last_line = line_index;
    }

    return strings.to_string(builder);
}

fix_lines :: proc(p: ^Printer) {
	align_var_decls(p);
	align_blocks(p);
	align_comments(p); //align them last since they rely on the other alignments
}

align_var_decls :: proc(p: ^Printer) {

}

align_switch_smt :: proc(p: ^Printer, index: int) {

	switch_found := false;
	brace_token: Format_Token;
	brace_line: int;

	found_switch_brace: for line, line_index in p.lines[index:] {

		for format_token in line.format_tokens {

			if format_token.kind == .Open_Brace && switch_found {
				brace_token = format_token;
				brace_line = line_index+index;
				break found_switch_brace;
			} else if format_token.kind == .Open_Brace {
				break;
			} else if format_token.kind == .Switch {
				switch_found = true;
			}

		}

	}

	if !switch_found {
		return;
	}

	largest := 0;
	case_count := 0;

	//find all the switch cases that are one lined
	for line, line_index in p.lines[brace_line+1:] {

		case_found := false;
		colon_found := false;
		length := 0;

		for format_token in line.format_tokens {

			if format_token.kind == .Comment {
				continue;
			}

			//this will only happen if the case is one lined
			if case_found && colon_found {
				largest = max(length, largest);
				break;
			}

			if format_token.kind == .Case {
				case_found = true;
				case_count += 1;
			} else if format_token.kind == .Colon {
				colon_found = true;
			} 

			length += len(format_token.text) + format_token.spaces_before;
		}

		if case_count >= brace_token.parameter_count {
			break;
		}

	}

	case_count = 0;

	for line, line_index in p.lines[brace_line+1:] {

		case_found := false;
		colon_found := false;
		length := 0;

		for format_token, i in line.format_tokens {

			if format_token.kind == .Comment {
				continue;
			}

			//this will only happen if the case is one lined
			if case_found && colon_found {
				line.format_tokens[i].spaces_before += (largest - length);
				break;
			}

			if format_token.kind == .Case {
				case_found = true;
				case_count += 1;
			} else if format_token.kind == .Colon {
				colon_found = true;
			} 

			length += len(format_token.text) + format_token.spaces_before;

		}

		if case_count >= brace_token.parameter_count {
			break;
		}
	}

}

align_struct :: proc(p: ^Printer, index: int) {

	struct_found := false;
	brace_token: Format_Token;
	brace_line: int;

	found_struct_brace: for line, line_index in p.lines[index:] {

		for format_token in line.format_tokens {

			if format_token.kind == .Open_Brace && struct_found {
				brace_token = format_token;
				brace_line = line_index+index;
				break found_struct_brace;
			} else if format_token.kind == .Open_Brace {
				break;
			} else if format_token.kind == .Struct {
				struct_found = true;
			}

		}

	}

	if !struct_found {
		return;
	}

	largest := 0;
	colon_count := 0;

	for line, line_index in p.lines[brace_line+1:] {

		length := 0;

		for format_token in line.format_tokens {

			if format_token.kind == .Comment {
				continue;
			}

			if format_token.kind == .Colon {
				colon_count += 1;
				largest = max(length, largest);
				break;
			} 

			length += len(format_token.text) + format_token.spaces_before;
		}

		if colon_count >= brace_token.parameter_count {
			break;
		}
	}

	colon_count = 0;

	for line, line_index in p.lines[brace_line+1:] {

		length := 0;

		for format_token, i in line.format_tokens {

			if format_token.kind == .Comment {
				continue;
			}

			if format_token.kind == .Colon {
				colon_count += 1;
				line.format_tokens[i+1].spaces_before = largest - length + 1;
				break;
			} 

			length += len(format_token.text) + format_token.spaces_before;
		}

		if colon_count >= brace_token.parameter_count {
			break;
		}
	}

}

align_blocks :: proc(p: ^Printer) {

	for line, line_index in p.lines {

		if len(line.format_tokens) <= 0 {
			continue;
		}

		if .Switch_Stmt in line.types && p.config.align_switch {
			align_switch_smt(p, line_index);
		} 
		
		if .Struct in line.types && p.config.align_structs {
			align_struct(p, line_index);
		}

	}

}

align_comments :: proc(p: ^Printer) {
	
	Comment_Align_Info :: struct {
		length: int,
		begin: int,
		end: int,
		depth: int,
	};

	comment_infos := make([dynamic]Comment_Align_Info, 0, context.temp_allocator);

	current_info: Comment_Align_Info;

	for line, line_index in p.lines {

		if len(line.format_tokens) <= 0 {
			continue;
		}

		if .Line_Comment in line.types {

			if current_info.end + 1 != line_index || current_info.depth != line.depth ||
			   (current_info.begin == current_info.end && current_info.length == 0) {

				if (current_info.begin != 0 && current_info.end != 0) || current_info.length > 0 {
					append(&comment_infos, current_info);
				}

				current_info.begin = line_index;
				current_info.end = line_index;
				current_info.depth = line.depth;
				current_info.length = 0;
			}

			length := 0;

			for format_token, i in line.format_tokens {

				if format_token.kind == .Comment {
					current_info.length = max(current_info.length, length);
					current_info.end = line_index;
				}

				length += format_token.spaces_before + len(format_token.text);
			}

		}

	}

	if (current_info.begin != 0 && current_info.end != 0) || current_info.length > 0 {
		append(&comment_infos, current_info);
	}

	for info in comment_infos {

		if info.begin == info.end || info.length == 0 {
			continue;
		}

		for i := info.begin; i <= info.end; i += 1 {

			l := p.lines[i];

			length := 0;

			for format_token, i in l.format_tokens {

				if format_token.kind == .Comment {
					if len(l.format_tokens) == 1 {
						l.format_tokens[i].spaces_before += info.length + 1;
					} else {
						l.format_tokens[i].spaces_before += info.length - length;
					}			
				}

				length += format_token.spaces_before + len(format_token.text);
			}

		}

	}

}