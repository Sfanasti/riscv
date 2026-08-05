/*
    Canale direzionato, profondita' 1, a transizione di livello.
    Controparte hardware di src/channel.h.

    Il registro fisico sta nel nodo PRODUTTORE: e' il suo OUT[d]. Il consumatore
    lo legge attraverso fili, ed e' il suo IN[opp(d)]. Per questo il modulo ha
    due lati di porte distinti: nel simulatore C la stessa asimmetria e' out_ch
    posseduto per valore contro in_ch che e' un puntatore.

    Stato codificato da due contatori mod 2, come in C:
        pieno / leggibile  :  wp != rp
        vuoto / scrivibile :  wp == rp
    Il produttore commuta wp per dire "c'e' un valore nuovo", il consumatore
    commuta rp per dire "l'ho preso". Nessun altro bit di stato: e' la forma
    richiesta in sede di revisione.

    ---- Cosa sparisce rispetto al C, ed e' meta' del senso dell'esercizio ----

    data_next / wp_next / rp_next e ch_commit() non esistono qui. Il doppio
    buffer che in C va emulato a mano per dare al vicino la latenza di un ciclo
    per hop e' la semantica nativa di "<=" dentro un always_ff: tutti i registri
    campionano il valore vecchio e cambiano insieme sul fronte. In hardware la
    fedelta' systolic e' gratis, in C costava tre campi in piu'.

    Sparisce anche la guardia "wp_next == wp" di ch_write. Se wr_en e pub_ok
    sono alti nello stesso ciclo, pub_reg <= out_reg campiona il valore VECCHIO
    e out_reg <= wr_data carica il nuovo: il non-blocking lo dà gratis. In C
    quella guardia serviva perche' grid_push poteva chiamare ch_write due volte
    nello stesso ciclo simulato, cosa che un ingresso campionato sul fronte non
    permette per costruzione. Era un problema dell'emulazione, non del
    protocollo.

    ---- Cosa invece COSTA piu' che in C, ed e' l'altra meta' ----

    I registri dati sono DUE, non uno. La OUT non e' piu' rifiutata a canale
    pieno (e' cosi' che si e' potuto togliere il bit "pending"), quindi il
    valore appena caricato e quello pubblicato-e-non-ancora-letto possono essere
    diversi nello stesso istante e non possono condividere gli stessi
    flip-flop. In C questo era gratis perche' data_next esisteva gia' per altri
    motivi; qui sono 32 flip-flop in piu' per canale, contro 1 bit risparmiato.
    E' un costo reale e va detto, non nascosto.
*/

`timescale 1ns / 1ps
`include "neso_defs.svh"

module neso_channel (
    input  logic        clk,
    input  logic        rst_n,      /* reset asincrono, attivo basso */

    /* lato produttore: le istruzioni OUT e SETRDY */
    input  logic        wr_en,      /* OUT    : carica il registro di uscita */
    input  logic [31:0] wr_data,
    input  logic        pub_en,     /* SETRDY : richiesta di pubblicazione */
    output logic        pub_ok,     /* esito di SETRDY, va in rd */
    output logic        iswrt,      /* wp == rp */

    /* lato consumatore: le istruzioni IN e ISRDY */
    input  logic        rd_en,      /* IN     : lettura consume-on-read */
    output logic [31:0] rd_data,
    output logic        isrdy       /* esito di ISRDY, va in rd */
);

    logic [31:0] out_reg;   /* registro di uscita   : lo carica OUT  (data_next in C) */
    logic [31:0] pub_reg;   /* registro pubblicato  : lo vede il vicino  (data in C) */
    logic        wp, rp;    /* contatori mod 2 */

    /*
        I due test sono puramente combinatori e senza effetti collaterali:
        ISRDY e ISWRT non consumano e non pubblicano niente. Sono lo STESSO
        comparatore, uno complementato rispetto all'altro. Costo: una porta XOR.
    */
    assign isrdy = (wp != rp);
    assign iswrt = (wp == rp);

    /* il dato e' sempre presentato sul filo; e' la lettura che consuma */
    assign rd_data = pub_reg;

    /*
        Test-and-set: l'esito che finisce in rd E' lo stesso filo che abilita i
        registri. E' questa fusione che rende SETRDY rifiutabile e ISRDY no —
        ISRDY non pilota nessun enable, quindi non c'e' niente da rifiutare.
    */
    assign pub_ok = pub_en & iswrt;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            out_reg <= 32'b0;
            pub_reg <= 32'b0;
            wp      <= 1'b0;
            rp      <= 1'b0;
        end
        else begin
            /*
                OUT: carica sempre, senza guardia. Puo' farlo perche' scrive
                out_reg, che il consumatore non vede mai: pub_reg e' protetto
                dal fatto che cambia SOLO alla transizione di wp, qui sotto.
            */
            if (wr_en) begin
                out_reg <= wr_data;
            end

            /*
                SETRDY: transizione di livello di wp, e sullo stesso fronte la
                cattura del dato. wp che commuta E' l'enable di pub_reg — non
                sono due meccanismi, e' un segnale solo.
            */
            if (pub_ok) begin
                pub_reg <= out_reg;
                wp      <= ~wp;
            end

            /* IN: consuma. Muove solo il puntatore, il dato resta dov'e'. */
            if (rd_en && isrdy) begin
                rp <= ~rp;
            end
        end
    end

endmodule
