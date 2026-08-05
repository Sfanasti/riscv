/*
    Due celle cablate come una griglia 1x2, cioe' il caso di asm/prodcons.s.
    Serve a far vedere due cose che il testbench del solo canale non mostra:

      - il DECODE: ogni cella riceve una operazione per ciclo (op, dir, dato) e
        ne restituisce un risultato solo, esattamente come una istruzione che
        scrive un registro;
      - il CABLAGGIO: fra le due celle corrono 33 fili in un verso (32 di dato
        piu' isrdy) e 1 nell'altro (lo strobe di consumo). Le assegnazioni qui
        sotto sono, letteralmente, quel fascio di fili.

    Le direzioni inutilizzate sono legate a zero: e' il bordo della griglia, e
    in C corrisponde ai canali che grid_init possiede e che l'host alimenta.
*/

`timescale 1ns / 1ps
`include "neso_defs.svh"

module tb_neso_if;

    logic clk = 1'b0;
    logic rst_n = 1'b0;

    /* comandi verso le due celle */
    logic        c0_valid, c1_valid;
    logic [1:0]  c0_op, c1_op, c0_dir, c1_dir;
    logic [31:0] c0_data, c1_data;
    logic [31:0] c0_res, c1_res;

    /* fasci di fili */
    logic [3:0][31:0] c0_o_rd_data, c0_i_rd_data, c1_o_rd_data, c1_i_rd_data;
    logic [3:0]       c0_o_isrdy, c0_o_rd_en, c0_i_isrdy, c0_i_rd_en;
    logic [3:0]       c1_o_isrdy, c1_o_rd_en, c1_i_isrdy, c1_i_rd_en;

    logic [31:0] res0, res1;
    int          errori = 0;

    neso_if c0 (
        .clk(clk), .rst_n(rst_n),
        .op_valid(c0_valid), .op(c0_op), .dir(c0_dir),
        .op_data(c0_data), .op_result(c0_res),
        .o_rd_data(c0_o_rd_data), .o_isrdy(c0_o_isrdy), .o_rd_en(c0_o_rd_en),
        .i_rd_data(c0_i_rd_data), .i_isrdy(c0_i_isrdy), .i_rd_en(c0_i_rd_en)
    );

    neso_if c1 (
        .clk(clk), .rst_n(rst_n),
        .op_valid(c1_valid), .op(c1_op), .dir(c1_dir),
        .op_data(c1_data), .op_result(c1_res),
        .o_rd_data(c1_o_rd_data), .o_isrdy(c1_o_isrdy), .o_rd_en(c1_o_rd_en),
        .i_rd_data(c1_i_rd_data), .i_isrdy(c1_i_isrdy), .i_rd_en(c1_i_rd_en)
    );

    /* ---- l'unico canale vivo: OUT[EST] di c0 == IN[OVEST] di c1 ---- */
    assign c1_i_rd_data[OVEST] = c0_o_rd_data[EST];   /* 32 fili */
    assign c1_i_isrdy  [OVEST] = c0_o_isrdy  [EST];   /*  1 filo  */
    assign c0_o_rd_en  [EST]   = c1_i_rd_en  [OVEST]; /*  1 filo, di ritorno */

    /* ---- tutto il resto e' bordo: niente vicino, niente dati ---- */
    assign c0_o_rd_en[NORD]  = 1'b0;
    assign c0_o_rd_en[SUD]   = 1'b0;
    assign c0_o_rd_en[OVEST] = 1'b0;
    assign c0_i_rd_data = '0;
    assign c0_i_isrdy   = '0;

    assign c1_o_rd_en = '0;
    assign c1_i_rd_data[NORD] = '0;
    assign c1_i_rd_data[EST]  = '0;
    assign c1_i_rd_data[SUD]  = '0;
    assign c1_i_isrdy[NORD]   = 1'b0;
    assign c1_i_isrdy[EST]    = 1'b0;
    assign c1_i_isrdy[SUD]    = 1'b0;

    always begin
        #5 clk = ~clk;
    end

    task automatic verifica(input logic cond, input string msg);
        if (cond) begin
            $display("  ok     : %0s", msg);
        end
        else begin
            $display("  FALLITO: %0s", msg);
            errori = errori + 1;
        end
    endtask

    /*
        Un ciclo di clock = una istruzione per cella, come nel simulatore C.
        La cella che non ha niente da fare esegue una ISRDY, che e' esattamente
        quello che fa il consumatore di prodcons.s mentre aspetta.
    */
    task automatic ciclo(
        input logic [1:0] op0, input logic [1:0] dir0, input logic [31:0] dato0,
        input logic [1:0] op1, input logic [1:0] dir1, input logic [31:0] dato1
    );
        @(negedge clk);
        c0_valid = 1'b1;  c0_op = op0;  c0_dir = dir0;  c0_data = dato0;
        c1_valid = 1'b1;  c1_op = op1;  c1_dir = dir1;  c1_data = dato1;
        #1;
        res0 = c0_res;
        res1 = c1_res;
    endtask

    initial begin
        $dumpfile("interfaccia.vcd");
        $dumpvars(0, tb_neso_if);

        c0_valid = 1'b0;  c0_op = OP_ISRDY;  c0_dir = NORD;  c0_data = 32'b0;
        c1_valid = 1'b0;  c1_op = OP_ISRDY;  c1_dir = NORD;  c1_data = 32'b0;

        rst_n = 1'b0;
        @(negedge clk);
        @(negedge clk);
        rst_n = 1'b1;

        $display("[1] c0 carica il registro di uscita verso EST");
        ciclo(OP_OUT,   EST, 32'd42,   OP_ISRDY, OVEST, 32'b0);
        verifica(res1 === 32'd0, "c1 non vede ancora niente");

        $display("[2] c0 pubblica");
        ciclo(OP_SETRDY, EST, 32'b0,   OP_ISRDY, OVEST, 32'b0);
        verifica(res0 === 32'd1, "SETRDY riesce: canale vuoto");
        verifica(res1 === 32'd0, "e c1 non lo vede nello stesso ciclo");

        $display("[3] un ciclo dopo il dato e' visibile: la latenza di un hop");
        ciclo(OP_ISRDY, NORD, 32'b0,   OP_ISRDY, OVEST, 32'b0);
        verifica(res1 === 32'd1, "ISRDY di c1 ora vale 1");

        $display("[4] c1 legge");
        ciclo(OP_ISRDY, NORD, 32'b0,   OP_IN,    OVEST, 32'b0);
        verifica(res1 === 32'd42, "IN restituisce 42, arrivato per filo");

        $display("[5] il consumo libera lo slot");
        ciclo(OP_ISRDY, NORD, 32'b0,   OP_ISRDY, OVEST, 32'b0);
        verifica(res1 === 32'd0, "canale di nuovo vuoto");

        $display("[6] e c0 puo' ripubblicare");
        ciclo(OP_SETRDY, EST, 32'b0,   OP_ISRDY, OVEST, 32'b0);
        verifica(res0 === 32'd1, "SETRDY riesce di nuovo");

        $display("");
        if (errori == 0) begin
            $display("tb_neso_if: OK");
        end
        else begin
            $display("tb_neso_if: %0d verifiche fallite", errori);
        end
        $finish;
    end

endmodule
