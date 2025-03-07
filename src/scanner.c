#include "tree_sitter/parser.h"
#include "tree_sitter/array.h"
#include "stdio.h"

typedef unsigned int uint;

enum TokenType {
  INDENT,
  OUTDENT,
  NEWLINE,
};

typedef struct {
  uint32_t size;
  uint16_t items[];
} SerializedScanner;

static void skip(TSLexer * lexer) {
  lexer->advance(lexer, false);
}

void * tree_sitter_moonscript_external_scanner_create() {
  Array(uint16_t) * stack = ts_calloc(1, sizeof(Array(uint16_t)));
  array_push(stack, 0);
  return stack;
}

void tree_sitter_moonscript_external_scanner_destroy(void * payload) {
  ts_free(payload);
}

uint tree_sitter_moonscript_external_scanner_serialize(void * payload, char * buffer) {
  Array(uint16_t) * stack = payload;
  SerializedScanner * output = (void *)buffer;

  output->size = stack->size;

  size_t nbytes = sizeof(uint16_t) * stack->size;
  memcpy(&output->items, stack->contents, nbytes);

  return sizeof(uint32_t) + nbytes;
}

void tree_sitter_moonscript_external_scanner_deserialize(void * payload, const char * buffer, uint length) {
  if (length < sizeof (uint32_t)) {
    return;
  }

  Array(uint16_t) * stack = payload;
  array_clear(stack);
  
  const SerializedScanner * input = (const void *)buffer;

  size_t nbytes = sizeof(uint16_t) * input->size;

  if (length < sizeof(uint32_t) + nbytes) {
    return;
  }

  array_extend(stack, input->size, &input->items);
}

bool tree_sitter_moonscript_external_scanner_scan(void * payload, TSLexer * lexer, const bool * valid_symbols) {
  Array(uint16_t) * indent_stack = payload;

  lexer->mark_end(lexer);

  bool found_eol = false;
  uint16_t indent = 0;
  uint16_t first_comment_indent = 0;
  bool found_first_comment = false;
  bool done = false;

  while (!done) {
    switch (lexer->lookahead) {
      case '\n':
        found_eol = true;
        indent = 0;
        skip(lexer);
        break;

      case ' ':
        indent += 1;
        skip(lexer);
        break;

      case '\r':
        indent = 0;
        skip(lexer);
        break;

      case '\t':
        indent += 4;
        skip(lexer);
        break;

      case '-':
        skip(lexer);
        if (lexer->lookahead == '-') {
          if (!found_first_comment) {
            found_first_comment = true;
          }
          while (lexer->lookahead != '\0' && lexer->lookahead != '\n') {
            skip(lexer);
          }
        } else {
          return false;
        }
        break;

      case '\0':
        indent = 0;
        found_eol = true;
        done = true;
        break;

      default:
        done = true;
        break;
    }
  }

  if (!found_eol && indent_stack->size > 0) {
    uint16_t current_indent = *array_back(indent_stack);

    if (valid_symbols[INDENT] && indent > current_indent) {
      array_push(indent_stack, indent);
      lexer->result_symbol = INDENT;
      lexer->mark_end(lexer);
      return true;
    }

    if (
      (valid_symbols[OUTDENT] /*|| !valid_symbols[NEWLINE]*/) &&
      indent < current_indent &&
      (!found_first_comment || first_comment_indent < current_indent)
    ) {
      array_pop(indent_stack);
      lexer->result_symbol = OUTDENT;
      lexer->mark_end(lexer);
      return true;
    }

    if (valid_symbols[NEWLINE]) {
      lexer->result_symbol = NEWLINE;
      lexer->mark_end(lexer);
      return true;
    }
  }

  return false;
}
