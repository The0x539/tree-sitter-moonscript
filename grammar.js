/**
 * @file Moonscript grammar for tree-sitter
 * @author The0x539 <a>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const separated = (t, s) => seq(t, repeat(prec.left(seq(s, t))));
const comma_separated = (t) => seq(
  t,
  repeat(seq(',', t)),
);
const comma_newline_separated = ($, t) => prec.right(seq(
  t,
  repeat(seq(',', optional($._newline), t)),
));

const optspace = optional(sym('_space'));
const prespace = t => seq(optspace, t);
const postspace = t => seq(t, optspace);
const spaced = t => seq(optspace, t, optspace);

const PREC = {
  CONDITION: 0,

  OR: 1, // or
  AND: 2, // and
  COMPARE: 3, // < > <= >= ~= != ==
  BIT_OR: 4, // |
  BIT_NOT: 5, // ~
  BIT_AND: 6, // &
  BIT_SHIFT: 7, // << >>
  CONCAT: 8, // ..
  ARITH_LOW: 9, // + -
  ARITH_HIGH: 10, // * / // %
  UNARY: 11, // not # - ~
  POWER: 12, // ^

  INVOKE: 13,
  DEFINE: 14, // idk
  INDEX: 15,
};

const do_stack = [true];

const disable_do = f => $ => {
  do_stack.push(false);
  const rule = f($);
  do_stack.pop();
  return rule;
};

const check_do = f => $ => {
  const top = do_stack[do_stack.length - 1];
  return top ? choice() : f($);
};

const primitives = {
  number: $ => new RustRegex('[0-9]+'),
  string: $ => choice($._single_string, $._double_string, $._shovel_string),
  _single_string: $ => new RustRegex(`'([^']|\\\\)*'`),
  _double_string: $ => new RustRegex(`"([^"]|\\\\)*"`), // TODO: interpolation
  // needs a custom scanner, but not frequently used
  _shovel_string: $ => choice(
    // new RustRegex('\\[(=*)\\[.*?\\]\\1\\]'), // would need the fancy_regex crate
    new RustRegex('\\[\\[.*?\\]\\]'),
    new RustRegex('\\[=\\[.*?\\]=\\]'),
    new RustRegex('\\[==\\[.*?\\]==\\]'),
    new RustRegex('\\[===\\[.*?\\]===\\]'),
  ),
};

const literals = {
  name: $ => new RustRegex('[A-Za-z_]+'),
  shebang: $ => seq('#!', field('path', $.shebang_path)),
  shebang_path: $ => new RustRegex('[^\\n]*'),

  _newline: $ => new RustRegex('\\r?\\n'),

  _space: $ => token(prec(100, choice(' ', '\t'))),
  comment: $ => prec.right(100, new RustRegex('--[^\\n]*')),

  // TODO: remove. adds a cutoff point to the test file, not a part of the actual grammar
  _conclusion: $ => new RustRegex('@@@NO FURTHER(.|\\n)*'),

  _literal: $ => choice(
    $.number,
    $.string,
    // $.function,
  ),
};

const parse = {
  block: $ => prec.right(repeat1(choice($._line, $._newline))),
  _line: $ => seq($.statement, $._newline),

  _body: $ => field('body', choice(
    seq($._newline, $._indent, $.block, $._outdent),
    $.statement,
  )),
  
  // TODO: decorators
  statement: $ => choice(
    // $.import,
    // $.while,
    // $.with,
    // $.for,
    // $.foreach,
    // $.switch,
    // $.return,
    // $.local,
    // $.export,
    // $.break_loop,
    $.assignment,
    $.expr_list,
    // $.compound_assignment,
  ),

  _condition: $ => field('condition', prec.left(PREC.CONDITION, choice($._expr, $.assignment))),

  if: $ => seq(
    'if',
    $._condition,
    optional('then'),
    $._body,
  ),

  expr_list: $ => prec.right(comma_separated($._expr)),

  assignment: $ => seq(
    field('lhs', $.assignment_lhs),
    alias('=', $.operator),
    field('rhs', $.assignment_rhs),
  ),

  assignment_lhs: $ => comma_separated($._place_expr),
  _place_expr: $ => choice(alias($.name, $.variable), $.index),

  // TODO: foo.bar indexing
  index: $ => prec(PREC.INDEX, seq($._expr, '[', $._expr, ']')),

  assignment_rhs: $ => prec.left(separated($._expr, choice(',', ';'))),

  _expr: $ => choice(
    $._literal,
    $.if,
    $._place_expr,
    // $.binary_expr,
    // $.unary_expr,
    $.function,
    seq('(', $._expr, ')'),
    $.invocation,
  ),

  binary_expr: $ => {
    const left_ops = [
      [PREC.OR, ['or']],
      [PREC.AND, ['and']],
      [PREC.COMPARE, ['<', '<=', '==', '~=', '!=', '>=', '>']],
      [PREC.BIT_OR, ['|']],
      [PREC.BIT_NOT, ['~']],
      [PREC.BIT_AND, ['&']],
      [PREC.BIT_SHIFT, ['<<', '>>']],
      [PREC.ARITH_LOW, ['+', '-']],
      [PREC.ARITH_HIGH, ['*', '/', '//', '%']],
    ];
    
    const right_ops = [
      [PREC.CONCAT, ['..']],
      [PREC.POWER, ['^']],
    ];
    
    const binary_expr = (operator) => seq(
      field('lhs', $._expr),
      alias(operator, $.operator),
      field('rhs', $._expr),
    );

    const rules = [];
    const binary_exprs = assoc => ([prec, ops]) => ops.map(binary_expr).map(rule => assoc(prec, rule));
    
    return choice(
      ...left_ops.flatMap(binary_exprs(prec.left)),
      ...right_ops.flatMap(binary_exprs(prec.right)),
    );
  },

  unary_expr: $ => {
    const operator = choice('not', '#', '-', '~');
    const operand = field('operand', $._expr);
    return prec.left(PREC.UNARY, seq(operator, operand));
  },

  invocation: $ => prec(PREC.INVOKE, seq(
    field('function', $._expr),
    field('args', $.invocation_args),
  )),

  invocation_args: $ => choice(
    $._invocation_arg_list,
    seq('(', optional($._invocation_arg_list), ')'),
    '!',
  ),

  _invocation_arg_list: $ => prec.right(comma_newline_separated($, $._expr)),

  definition_args: $ => prec(PREC.DEFINE, choice(
    $.name,
    seq(
      '(',
      comma_separated($.definition_arg),
      // todo: the "using" keyword
      prespace(')'),
    ),
  )),
  definition_arg: $ => seq(
    $.name,
    optional(seq('=', field('default', $._expr))),
  ),

  function: $ => seq(
    optional($.definition_args),
    choice('->', '=>'),
    choice($._body, $._newline),
  ),
};

const rules = {
  source_file: $ => seq(
    optional($.shebang),
    choice($.block),
    optional($._conclusion),
  ),

  ...literals,
  ...primitives,
  ...parse,
};

module.exports = grammar({
  name: "moonscript",

  extras: $ => [$.comment, ' '],

  externals: $ => [$._indent, $._outdent],
  
  conflicts: $ => [
    // [$.binary_expr, $.unary_expr, $.invocation],
    // [$.binary_expr, $.invocation],
    // [$.invocation, $.definition_arg],
    // [$.definition_args, $.definition_arg_list],
    // the real conflicts
    [$.assignment_lhs, $._expr],
    // [$._place_expr, $.definition_args],
    [$._place_expr, $.definition_arg],
    // [$.definition_args, $.invocation_args],
  ],

  reserved: {
    global: $ => ['then'],
  },
  
  rules,
});
