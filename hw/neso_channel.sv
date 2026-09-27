/* controparte di src/channel.h */

`timescale 1ns / 1ps
`include "neso_defs.svh"

module neso_channel (
    input  logic        clk,
    input  logic        rst_n,      /* reset asincrono, attivo basso */

    /* lato produttore: OUT e SETRDY */
    input  logic        wr_en,      /* OUT */
    input  logic [31:0] wr_data,
    input  logic        pub_en,     /* SETRDY */
    output logic        pub_ok,     /* esito di SETRDY */
    output logic        iswrt,

    /* lato consumatore: IN e ISRDY */
    input  logic        rd_en,      /* IN */
    output logic [31:0] rd_data,
    output logic        isrdy       /* esito di ISRDY */
);

    logic [31:0] out_reg;   /* data_next in C */
    logic [31:0] pub_reg;   /* data in C */
    logic        wp, rp;    /* contatori mod 2 */

    assign isrdy = (wp != rp);
    assign iswrt = (wp == rp);

    assign rd_data = pub_reg;

    assign pub_ok = pub_en & iswrt;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            out_reg <= 32'b0;
            pub_reg <= 32'b0;
            wp      <= 1'b0;
            rp      <= 1'b0;
        end
        else begin
            /* OUT */
            if (wr_en) begin
                out_reg <= wr_data;
            end

            /* SETRDY */
            if (pub_ok) begin
                pub_reg <= out_reg;
                wp      <= ~wp;
            end

            /* IN */
            if (rd_en && isrdy) begin
                rp <= ~rp;
            end
        end
    end

endmodule
