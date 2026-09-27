/* header e non package: il frontend Verilog di yosys non supporta l'import */

`ifndef NESO_DEFS_SVH
`define NESO_DEFS_SVH

/* le direzioni le usano solo i testbench */
/* verilator lint_off UNUSEDPARAM */

/* i funct3 dell'opcode custom-0, come in asm/macros.s */
localparam logic [1:0] OP_IN     = 2'd0;
localparam logic [1:0] OP_OUT    = 2'd1;
localparam logic [1:0] OP_ISRDY  = 2'd2;
localparam logic [1:0] OP_SETRDY = 2'd3;

/* come in src/risc.h */
localparam logic [1:0] NORD  = 2'd0;
localparam logic [1:0] EST   = 2'd1;
localparam logic [1:0] SUD   = 2'd2;
localparam logic [1:0] OVEST = 2'd3;

/* verilator lint_on UNUSEDPARAM */

`endif
