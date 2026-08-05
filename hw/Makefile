# MAKEFILE HW
#
#   make          compila ed esegue i due testbench
#   make lint     verilator --lint-only: SV pulito, non solo "iverilog-pulito"
#   make stat     yosys: conteggio REALE dei flip-flop del canale
#   make wave     apre il waveform del canale in gtkwave
#   make clean
#
# Serve la toolchain:  sudo apt install iverilog gtkwave verilator yosys
#
# I moduli sono la controparte hardware di src/channel.h: neso_channel.sv e' il
# canale, neso_if.sv e' l'interfaccia completa di una cella (quattro canali piu'
# il decode delle quattro operazioni).

IVFLAGS = -g2012 -Wall
RTL     = neso_channel.sv neso_if.sv

.PHONY: all lint stat wave clean

all: sim-channel sim-if

# ===== SIMULAZIONE =========================================================

sim-channel: neso_channel.sv tb_neso_channel.sv neso_defs.svh
	@iverilog $(IVFLAGS) -o tb_ch neso_channel.sv tb_neso_channel.sv
	@vvp tb_ch

sim-if: $(RTL) tb_neso_if.sv neso_defs.svh
	@iverilog $(IVFLAGS) -o tb_if $(RTL) tb_neso_if.sv
	@vvp tb_if

# ===== CONTROLLO ===========================================================
# iverilog e' permissivo: accetta in silenzio cose che un tool serio rifiuta.
# Questo e' il controllo vero sulla conformita' SystemVerilog.

lint: $(RTL)
	verilator --lint-only -Wall --top-module neso_if $(RTL)
	@echo "lint: OK"

# ===== SINTESI =============================================================
# Attesi 66 flip-flop: 32 di out_reg, 32 di pub_reg, wp e rp. Piu' una manciata
# di porte, di cui XOR e XNOR sono i due comparatori isrdy/iswrt.
#
# La sintesi si ferma al canale perche' il frontend Verilog di yosys non regge
# gli array packed multidimensionali nelle liste di porte, che neso_if usa per
# tenere leggibile il fascio di fili. Non e' una perdita: neso_if istanzia
# quattro canali e null'altro di sequenziale — il decode e' puramente
# combinatorio (always_comb) — quindi sono esattamente 4 x 66 = 264 flip-flop.

stat: neso_channel.sv
	@yosys -p 'read_verilog -sv neso_channel.sv; synth -top neso_channel; stat' 2>&1 \
	    | awk '/^End of script/{exit} /^=== /{b=""} {b = b $$0 "\n"} END{printf "%s", b}'

# ===== UTILITY =============================================================

wave: sim-channel
	gtkwave canale.vcd

clean:
	rm -f tb_ch tb_if canale.vcd interfaccia.vcd
