# MAKEFILE HW
#
#   make          compila ed esegue i due testbench
#   make lint     verilator --lint-only
#   make stat     yosys: flip-flop del canale
#   make wave     apre canale.vcd in gtkwave
#   make clean
#
# Toolchain: sudo apt install iverilog gtkwave verilator yosys

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
# iverilog è permissivo: la conformità SystemVerilog la controlla verilator.

lint: $(RTL)
	verilator --lint-only -Wall --top-module neso_if $(RTL)
	@echo "lint: OK"

# ===== SINTESI =============================================================
# Attesi 66 flip-flop (out_reg, pub_reg, wp, rp). Solo il canale: yosys non
# legge le porte packed multidimensionali di neso_if, che ha 4 x 66 flip-flop.

stat: neso_channel.sv
	@yosys -p 'read_verilog -sv neso_channel.sv; synth -top neso_channel; stat' 2>&1 \
	    | awk '/^End of script/{exit} /^=== /{b=""} {b = b $$0 "\n"} END{printf "%s", b}'

# ===== UTILITY =============================================================

wave: sim-channel
	gtkwave canale.vcd

clean:
	rm -f tb_ch tb_if canale.vcd interfaccia.vcd
