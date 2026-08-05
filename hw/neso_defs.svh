/*
    Costanti condivise fra i moduli e i testbench.
    Valori identici a quelli del simulatore C, di proposito: se cambiano li',
    devono cambiare qui.

    Header incluso e non package: il frontend Verilog di yosys non supporta
    l'import di package, e la sintesi qui serve a contare i flip-flop davvero
    invece che a mano. Un `include lo leggono tutti e tre i tool.
*/

`ifndef NESO_DEFS_SVH
`define NESO_DEFS_SVH

/* le direzioni le usano i testbench, non i moduli: per verilator sono "unused" */
/* verilator lint_off UNUSEDPARAM */

/*
    Le quattro operazioni dell'interfaccia. I valori SONO i funct3 dell'opcode
    custom-0 0x0B, si veda asm/macros.s: qui non si sta scegliendo una
    codifica, si sta rispecchiando quella dell'ISA.
*/
localparam logic [1:0] OP_IN     = 2'd0;
localparam logic [1:0] OP_OUT    = 2'd1;
localparam logic [1:0] OP_ISRDY  = 2'd2;
localparam logic [1:0] OP_SETRDY = 2'd3;

/* direzioni, come i #define di src/risc.h */
localparam logic [1:0] NORD  = 2'd0;
localparam logic [1:0] EST   = 2'd1;
localparam logic [1:0] SUD   = 2'd2;
localparam logic [1:0] OVEST = 2'd3;

/* verilator lint_on UNUSEDPARAM */

`endif
