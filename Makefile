GCC = gcc
CFLAGS = -Wall -pthread

all: controlador agente

controlador: controlador.c
	$(CC) $(CFLAGS) controlador.c -o controlador

agente: agente.c
	$(CC) $(CFLAGS) agente.c -o agente

clean:
	rm -f controlador agente /tmp/parque_pipe
