/*********************************************************************
 * AGENTE DE RESERVAS - VERSIÓN FINAL CORREGIDA Y FUNCIONANDO 100%
 *********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#define BUFFER 512

int main(int argc, char *argv[]) {
    char nombre[64] = "";
    char archivo[256] = "";
    char pipe_principal[256] = "";
    
    int opt;
    while ((opt = getopt(argc, argv, "s:a:p:")) != -1) {
        switch (opt) {
            case 's': strncpy(nombre, optarg, sizeof(nombre)-1); break;
            case 'a': strncpy(archivo, optarg, sizeof(archivo)-1); break;
            case 'p': strncpy(pipe_principal, optarg, sizeof(pipe_principal)-1); break;
        }
    }

    if(nombre[0]==0 || archivo[0]==0 || pipe_principal[0]==0){
        fprintf(stderr, "Uso: %s -s <nombre> -a <archivo.csv> -p <pipe>\n", argv[0]);
        return 1;
    }

    /* 1. Crear pipe personal */
    char pipe_personal[256];
    snprintf(pipe_personal, sizeof(pipe_personal), "/tmp/pipe_%s", nombre);
    unlink(pipe_personal);
    if (mkfifo(pipe_personal, 0666) == -1) {
        perror("mkfifo");
        return 1;
    }

    printf("[AGENTE %s] Iniciando... pipe personal: %s\n", nombre, pipe_personal);

    /* 2. REGISTRO */
    int fd = open(pipe_principal, O_WRONLY);
    if (fd == -1) { perror("open principal"); return 1; }
    
    char mensaje[BUFFER];
    snprintf(mensaje, sizeof(mensaje), "REGISTRO|%s|%s", nombre, pipe_personal);
    write(fd, mensaje, strlen(mensaje)+1);
    close(fd);

    /* 3. Esperar hora actual */
    int fd_resp = open(pipe_personal, O_RDONLY);
    if (fd_resp == -1) { perror("open personal"); return 1; }

    char resp[BUFFER];
    read(fd_resp, resp, BUFFER);
    int hora_actual = 0;
    sscanf(resp, "HORA_ACTUAL|%d", &hora_actual);
    printf("[AGENTE %s] Hora actual del parque: %d:00\n", nombre, hora_actual);

    /* 4. Procesar archivo CSV */
    FILE *f = fopen(archivo, "r");
    if (!f) { perror("fopen csv"); return 1; }

    char linea[256];
    while (fgets(linea, sizeof(linea), f)) {
        char familia[100];
        int hora_sol, personas;

        if (sscanf(linea, "%99[^,],%d,%d", familia, &hora_sol, &personas) != 3)
            continue;

        if (hora_sol < hora_actual) {
            printf("[AGENTE %s] Omitiendo reserva pasada: %s (%d:00)\n", nombre, familia, hora_sol);
            continue;
        }

        printf("[AGENTE %s] → Reserva: %s, %d:00, %d personas\n", nombre, familia, hora_sol, personas);

        fd = open(pipe_principal, O_WRONLY);
        snprintf(mensaje, sizeof(mensaje), "SOLICITUD|%s|%s|%d|%d", nombre, familia, hora_sol, personas);
        write(fd, mensaje, strlen(mensaje)+1);
        close(fd);

        read(fd_resp, resp, BUFFER);
        printf("[AGENTE %s] ← %s\n", nombre, resp);

        sleep(2);
    }

    fclose(f);
    close(fd_resp);
    unlink(pipe_personal);
    printf("Agente %s termina.\n", nombre);
    return 0;
}
