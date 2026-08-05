/*
    Interfaccia di UNA cella NESO: i registri di confine e le quattro
    operazioni che ci agiscono sopra. E' la risposta alla domanda "cosa
    servirebbe, in hardware, per implementare l'interfaccia di una cella".

    La cella possiede i propri QUATTRO canali di uscita (uno per direzione) e
    li istanzia; dei canali dei vicini vede solo i fili. In una griglia ogni
    canale e' quindi istanziato una volta sola, dal suo produttore — lo stesso
    che in C fa grid_init, dove out_ch e' posseduto per valore e in_ch e' un
    puntatore al vicino.

    ---- Il costo, che e' il deliverable ----

                            per canale   per cella (x4)   griglia 12x12
        flip-flop           32+32+1+1=66      264             38.016
        fili verso il vicino      33          132
        fili dal vicino            1            4
        combinatoria         1 XOR + 1 AND

    I 33 fili in uscita sono i 32 del dato piu' isrdy; l'unico filo di ritorno
    e' lo strobe di consumo. Fra due celle adiacenti corrono quindi 68 fili in
    tutto, 34 per verso.

    ---- Una scelta di modellazione, non una svista ----

    neso_channel tiene ENTRAMBI i contatori, wp e rp, come fa la struct Channel
    in C. Di conseguenza il consumatore manda al canale uno strobe (rd_en)
    invece di far attraversare il proprio rp come livello. Fisicamente si
    potrebbe mettere il flip-flop rp nella cella consumatrice: sarebbe piu'
    fedele alla transizione di livello pura, ma spezzerebbe il modulo fra due
    celle e romperebbe il parallelo 1:1 con src/channel.h, che qui vale di piu'.
    Il numero di flip-flop non cambia: si sposta soltanto.
*/

`timescale 1ns / 1ps
`include "neso_defs.svh"

module neso_if (
    input  logic             clk,
    input  logic             rst_n,

    /* ---- lato core: una operazione per ciclo, come una istruzione ---- */
    input  logic             op_valid,
    input  logic [1:0]       op,          /* OP_IN | OP_OUT | OP_ISRDY | OP_SETRDY */
    input  logic [1:0]       dir,
    input  logic [31:0]      op_data,     /* rs1 della OUT */
    output logic [31:0]      op_result,   /* cio' che va in rd: dato o esito */

    /* ---- OUT[d]: io produco, il vicino consuma ---- */
    output logic [3:0][31:0] o_rd_data,
    output logic [3:0]       o_isrdy,
    input  logic [3:0]       o_rd_en,

    /* ---- IN[d]: il vicino produce, io consumo ---- */
    input  logic [3:0][31:0] i_rd_data,
    input  logic [3:0]       i_isrdy,
    output logic [3:0]       i_rd_en
);

    logic [3:0] wr_en, pub_en, pub_ok;

    /*
        I quattro canali di uscita. wr_data arriva a tutti e quattro: e' solo
        fanout, non serve un multiplexer, perche' a catturarlo e' unicamente
        quello con wr_en alto.
    */
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
                /*
                    iswrt resta scollegato di proposito: a livello di cella
                    l'informazione arriva gia' da pub_ok, che e' iswrt filtrato
                    da pub_en. La porta serve al canale usato da solo (il suo
                    testbench la controlla), e nel waveform si vede comunque
                    dentro l'istanza.
                */
                /* verilator lint_off PINCONNECTEMPTY */
                .iswrt   (),
                /* verilator lint_on PINCONNECTEMPTY */
                .rd_en   (o_rd_en[d]),
                .rd_data (o_rd_data[d]),
                .isrdy   (o_isrdy[d])
            );
        end
    endgenerate

    /*
        Il decode: e' tutta la logica di controllo dell'interfaccia, e rende
        visibile l'asimmetria fra i due test.

          OP_ISRDY  legge un filo e basta: non alza nessun enable, quindi non
                    c'e' niente che possa essere rifiutato. Puo' valere 0, ma
                    zero e' una risposta, non un rifiuto.
          OP_SETRDY alza un enable E ne restituisce l'esito: e' un test-and-set,
                    l'unica operazione che possa fallire.
          OP_IN     occupa tutti i 32 bit di op_result col dato letto, e per
                    questo il suo test DEVE stare in una istruzione separata:
                    una istruzione RISC-V scrive un solo registro.
          OP_OUT    alza un enable e non restituisce niente (rd = x0).
    */
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
                    op_result   = {31'b0, pub_ok[dir]};  /* l'esito di ch_setrdy() */
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
