/*
    Testbench di neso_channel.sv: riproduce la sequenza di asm/prodcons.s su un
    solo canale e verifica le proprieta' che il protocollo deve garantire.
    Se il modulo e il modello C di src/channel.h concordano, passa.

    Gli stessi casi limite sono asseriti in tests/test_channel.c: questo e' il
    gemello hardware di quel file, incluso il caso [5], che e' il test di
    regressione della rimozione del bit "pending".
*/

`timescale 1ns / 1ps
`include "neso_defs.svh"

module tb_neso_channel;

    logic        clk = 1'b0;
    logic        rst_n = 1'b0;
    logic        wr_en = 1'b0;
    logic [31:0] wr_data = 32'b0;
    logic        pub_en = 1'b0;
    logic        rd_en = 1'b0;

    logic        pub_ok;
    logic        iswrt;
    logic [31:0] rd_data;
    logic        isrdy;

    logic        esito;
    logic [31:0] letto;
    int          errori = 0;

    neso_channel dut (
        .clk     (clk),
        .rst_n   (rst_n),
        .wr_en   (wr_en),
        .wr_data (wr_data),
        .pub_en  (pub_en),
        .pub_ok  (pub_ok),
        .iswrt   (iswrt),
        .rd_en   (rd_en),
        .rd_data (rd_data),
        .isrdy   (isrdy)
    );

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
        Le operazioni del produttore/consumatore. Ognuna tiene il proprio enable
        alto per un solo fronte di salita, come farebbe il decoder che esegue una
        istruzione per ciclo. L'esito si campiona subito dopo aver alzato
        l'enable: e' combinatorio e riflette lo stato che il fronte usera'.
    */
    task automatic op_out(input logic [31:0] v);
        @(negedge clk);
        wr_en   = 1'b1;
        wr_data = v;
        @(negedge clk);
        wr_en = 1'b0;
        $display("  OUT    valore=%0d", v);
    endtask

    task automatic op_setrdy;
        @(negedge clk);
        pub_en = 1'b1;
        #1 esito = pub_ok;
        @(negedge clk);
        pub_en = 1'b0;
        $display("  SETRDY esito=%0d", esito);
    endtask

    task automatic op_in;
        @(negedge clk);
        rd_en = 1'b1;
        #1 letto = rd_data;
        @(negedge clk);
        rd_en = 1'b0;
        $display("  IN     valore=%0d", letto);
    endtask

    /* ISRDY non alza nessun enable: legge un filo e basta */
    task automatic op_isrdy;
        @(negedge clk);
        #1 esito = isrdy;
        $display("  ISRDY  esito=%0d", esito);
    endtask

    initial begin
        $dumpfile("canale.vcd");
        $dumpvars(0, tb_neso_channel);

        rst_n = 1'b0;
        @(negedge clk);
        @(negedge clk);
        rst_n = 1'b1;
        @(negedge clk);

        $display("[1] dopo il reset il canale e' vuoto");
        verifica(iswrt === 1'b1, "iswrt alto");
        verifica(isrdy === 1'b0, "isrdy basso");

        $display("[2] il produttore carica e pubblica");
        op_out(32'd42);
        op_setrdy;
        verifica(esito === 1'b1, "SETRDY su slot libero ritorna 1");
        op_isrdy;
        verifica(esito === 1'b1, "il consumatore vede il dato il ciclo dopo");

        $display("[3] backpressure: la OUT entra, ma non scavalca il dato pubblicato");
        op_out(32'd99);
        op_setrdy;
        verifica(esito === 1'b0, "SETRDY su slot pieno ritorna 0");
        op_isrdy;
        verifica(esito === 1'b1, "il canale e' ancora pieno");
        verifica(rd_data === 32'd42, "il dato pubblicato non e' stato toccato");
        verifica(dut.out_reg === 32'd99, "ma il 99 e' nel registro di uscita");

        $display("[4] il consumatore legge e libera lo slot");
        op_in;
        verifica(letto === 32'd42, "IN restituisce il valore pubblicato");
        op_isrdy;
        verifica(esito === 1'b0, "canale di nuovo vuoto");
        verifica(iswrt === 1'b1, "e di nuovo scrivibile");

        /*
            Il caso che distingue questo progetto da quello con "pending".
            Nessuna OUT qui davanti: il 99 e' rimasto nel registro di uscita dal
            punto [3], caricato mentre il canale era pieno, e la prima SETRDY che
            riesce lo consegna. Con "pending" la OUT del punto [3] sarebbe stata
            rifiutata e questa SETRDY avrebbe ripubblicato il 42.
        */
        $display("[5] regressione: SETRDY senza OUT consegna il valore gia' caricato");
        op_setrdy;
        verifica(esito === 1'b1, "SETRDY riesce a canale vuoto");
        op_in;
        verifica(letto === 32'd99, "ed esce il 99, non un 42 duplicato");

        $display("");
        if (errori == 0) begin
            $display("tb_neso_channel: OK");
        end
        else begin
            $display("tb_neso_channel: %0d verifiche fallite", errori);
        end
        $finish;
    end

endmodule
