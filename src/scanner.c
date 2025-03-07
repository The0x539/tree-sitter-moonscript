#include "tree_sitter/parser.h"
#include "tree_sitter/array.h"
#include "stdio.h"

typedef unsigned int uint;

enum TokenType {
  NEWLINE,
  INDENT,
  OUTDENT,
};

typedef struct {
  uint32_t size;
  uint16_t items[];
} SerializedScanner;

static void skip(TSLexer * lexer) {
  lexer->advance(lexer, true);
}

static void advance(TSLexer * lexer) {
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
  printf("\n");
  printf("hi\n");
  
  Array(uint16_t) * indent_stack = payload;
  if (indent_stack->size == 0) {
    printf("WARN: indent stack was empty\n");
    array_push(indent_stack, 0);
  }

  // lexer->mark_end(lexer);

  bool found_eol = false;
  uint16_t indent = 0;
  bool found_first_comment = false;
  uint16_t first_comment_indent = 0; // todo: whatever man

  while (true) {
    char c = lexer->lookahead;
    if (c == '\n') {
      printf("hit LF\n");
      found_eol = true;
      indent = 0;
      advance(lexer);
    } else if (c == ' ') {
      printf("hit space\n");
      indent += 1;
      advance(lexer);
    } else if (c == '\r' || c == '\f') {
      printf("hit CR\n");
      indent = 0;
      skip(lexer);
    } else if (c == '\t') {
      printf("hit tab\n");
      indent += 4;
      advance(lexer);
    } else if (c == '-' && (valid_symbols[INDENT] || valid_symbols[OUTDENT] || valid_symbols[NEWLINE])) {
      skip(lexer);
      if (lexer->lookahead == '-') {
        if (!found_first_comment) {
          printf("comment\n");
          found_first_comment = true;
        }
        while (lexer->lookahead != '\n') {
          skip(lexer);
        }
        skip(lexer);
        indent = 0;
      } else {
        printf("hit %c after -\n", lexer->lookahead);
        return false;
      }
    } else if (lexer->eof(lexer)) {
      printf("EOF\n");
      found_eol = true;
      indent = 0;
      lexer->mark_end(lexer);
      return false;
      break;
    } else {
      printf("hit %c\n", c);
      break;
    }
  }

  if (!found_eol) {
    printf("did not find eol\n");
    return false;
  }

  uint16_t prev_indent = *array_back(indent_stack);

  printf("%b %d %d\n", found_eol, prev_indent, indent);
  printf("%b %b %b\n", valid_symbols[INDENT], valid_symbols[OUTDENT], valid_symbols[NEWLINE]);

  if (valid_symbols[INDENT] && indent > prev_indent) {
    printf("indent\n");
    array_push(indent_stack, indent);
    lexer->result_symbol = INDENT;
    return true;
  }

  if (
    (valid_symbols[OUTDENT] /*|| !valid_symbols[NEWLINE]*/) &&
    indent < prev_indent &&
    (!found_first_comment || first_comment_indent < prev_indent)
  ) {
    printf("outdent\n");
    array_pop(indent_stack);
    lexer->result_symbol = OUTDENT;
    return true;
  }

  if (valid_symbols[NEWLINE]) {
    printf("newline\n");
    lexer->result_symbol = NEWLINE;
    return true;
  }

  printf("nothing");

  return false;
}
