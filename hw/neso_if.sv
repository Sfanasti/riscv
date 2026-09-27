/* interfaccia di una cella: i quattro canali di uscita e la decodifica */

`timescale 1ns / 1ps
`include "neso_defs.svh"

module neso_if (
    input  logic             clk,
    input  logic             rst_n,

    /* lato core: una operazione per ciclo */
    input  logic             op_valid,
    input  logic [1:0]       op,          /* OP_* di neso_defs.svh */
    input  logic [1:0]       dir,
    input  logic [31:0]      op_data,     /* rs1 della OUT */
    output logic [31:0]      op_result,   /* ciò che va in rd */

    /* OUT[d]: la cella produce, il vicino consuma */
    output logic [3:0][31:0] o_rd_data,
    output logic [3:0]       o_isrdy,
    input  logic [3:0]       o_rd_en,

    /* IN[d]: il vicino produce, la cella consuma */
    input  logic [3:0][31:0] i_rd_data,
    input  logic [3:0]       i_isrdy,
    output logic [3:0]       i_rd_en
);

    logic [3:0] wr_en, pub_en, pub_ok;

    genvar d;
    generate
        for (d = 0; d < 4; d++) begin : ch
            neso_channel u_ch (
                .clk     (clk),
                .rst_n   (rst_n),
                .wr_en   (wr_en[d]),
                .wr_data (op_data),
                .pub_en  (pub_en[d]),
                .pub_ok  (pub_ok[d]),
                /* verilator lint_off PINCONNECTEMPTY */
                .iswrt   (),
                /* verilator lint_on PINCONNECTEMPTY */
                .rd_en   (o_rd_en[d]),
                .rd_data (o_rd_data[d]),
                .isrdy   (o_isrdy[d])
            );
        end
    endgenerate

    always_comb begin
        wr_en     = 4'b0;
        pub_en    = 4'b0;
        i_rd_en   = 4'b0;
        op_result = 32'b0;

        if (op_valid) begin
            case (op)
                OP_OUT: begin
                    wr_en[dir] = 1'b1;
                end
                OP_SETRDY: begin
                    pub_en[dir] = 1'b1;
                    op_result   = {31'b0, pub_ok[dir]};
                end
                OP_ISRDY: begin
                    op_result = {31'b0, i_isrdy[dir]};
                end
                OP_IN: begin
                    i_rd_en[dir] = 1'b1;
                    op_result    = i_rd_data[dir];
                end
                default: begin
                end
            endcase
        end
    end

endmodule
