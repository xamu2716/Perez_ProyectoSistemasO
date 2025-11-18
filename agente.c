/**************************************************************
* Proyecto: Sistema de Reservas – Parque Berlín (2025-30)
* Archivo: Agente de Reservas (Cliente)
* Autores: Juan Felipe Gómez López, Xamuel Pérez, David Beltrán
*
* Descripción corta:
*   Este programa representa un “Agente”. Cada agente lee un
*   archivo CSV con reservas y se comunica con el Controlador
*   usando pipes FIFO. El agente envía solicitudes y espera
*   respuestas del Controlador.
**************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>    // getopt(), sleep(), read(), write()
#include <fcntl.h>     // open()
#include <sys/stat.h>  // mkfifo()
#include <string.h>    // strncpy(), snprintf()

#define BUFFER 512     // Tamaño general de buffers para mensajes

int main(int argc, char *argv[]) {

    /* Variables donde se guardarán los parámetros del agente */
    char nombre[64] = "";
    char archivo[256] = "";
    char pipe_principal[256] = "";
    
    /* ---------------------------------------------------------
       1. LEER PARÁMETROS USANDO GETOPT
       -s : nombre del agente
       -a : archivo CSV con solicitudes
       -p : pipe por el que se comunica con el controlador
       --------------------------------------------------------- */
    int opt;
    while ((opt = getopt(argc, argv, "s:a:p:")) != -1) {
        switch (opt) {
            case 's': strncpy(nombre, optarg, sizeof(nombre)-1); break;
            case 'a': strncpy(archivo, optarg, sizeof(archivo)-1); break;
            case 'p': strncpy(pipe_principal, optarg, sizeof(pipe_principal)-1); break;
        }
    }

    /* Validamos que sí se hayan pasado los tres parámetros */
    if (nombre[0]==0 || archivo[0]==0 || pipe_principal[0]==0) {
        fprintf(stderr, "Uso: %s -s <nombre> -a <archivo.csv> -p <pipe>\n", argv[0]);
        return 1;
    }

    /* ---------------------------------------------------------
       2. CREAR PIPE PERSONAL DEL AGENTE
       Este pipe es para recibir respuestas del controlador.
       Ejemplo: /tmp/pipe_Juan
       --------------------------------------------------------- */
    char pipe_personal[256];
    snprintf(pipe_personal, sizeof(pipe_personal), "/tmp/pipe_%s", nombre);

    unlink(pipe_personal); // borrar si ya existía de ejecuciones anteriores
    if (mkfifo(pipe_personal, 0666) == -1) { // crear el FIFO
        perror("mkfifo");
        return 1;
    }

    printf("[AGENTE %s] Iniciando... pipe personal: %s\n", nombre, pipe_personal);

    /* ---------------------------------------------------------
       3. REGISTRO DEL AGENTE EN EL CONTROLADOR
       Enviamos el mensaje:
          REGISTRO|nombre|pipe_personal
       --------------------------------------------------------- */
    int fd = open(pipe_principal, O_WRONLY);
    if (fd == -1) {
        perror("open principal");
        return 1;
    }
    
    char mensaje[BUFFER];
    snprintf(mensaje, sizeof(mensaje), "REGISTRO|%s|%s", nombre, pipe_personal);
    write(fd, mensaje, strlen(mensaje)+1);  // +1 para enviar '\0'
    close(fd);

    /* ---------------------------------------------------------
       4. ESPERAR LA RESPUESTA DEL CONTROLADOR
       Recibimos: HORA_ACTUAL|<num>
       --------------------------------------------------------- */
    int fd_resp = open(pipe_personal, O_RDONLY);
    if (fd_resp == -1) {
        perror("open personal");
        return 1;
    }

    char resp[BUFFER];
    read(fd_resp, resp, BUFFER); // leer mensaje de bienvenida con la hora

    int hora_actual = 0;
    sscanf(resp, "HORA_ACTUAL|%d", &hora_actual); // extraer la hora
    printf("[AGENTE %s] Hora actual del parque: %d:00\n", nombre, hora_actual);

    /* ---------------------------------------------------------
       5. ABRIR Y LEER EL ARCHIVO CSV
       Cada línea es: familia,hora,personas
       --------------------------------------------------------- */
    FILE *f = fopen(archivo, "r");
    if (!f) {
        perror("fopen csv");
        return 1;
    }

    char linea[256];

    /* ---------------------------------------------------------
       6. PROCESAR CADA RESERVA DEL CSV
       --------------------------------------------------------- */
    while (fgets(linea, sizeof(linea), f)) {

        char familia[100];
        int hora_sol, personas;

        /* Extraemos los datos de la línea */
        if (sscanf(linea, "%99[^,],%d,%d", familia, &hora_sol, &personas) != 3)
            continue; // línea inválida → la saltamos

        /* No enviar reservas para horas que ya pasaron */
        if (hora_sol < hora_actual) {
            printf("[AGENTE %s] Omitiendo reserva pasada: %s (%d:00)\n",
                   nombre, familia, hora_sol);
            continue;
        }

        printf("[AGENTE %s] → Reserva: %s, %d:00, %d personas\n",
               nombre, familia, hora_sol, personas);

        /* -----------------------------------------------------
           7. ENVIAR SOLICITUD AL CONTROLADOR
           Formato:
           SOLICITUD|nombre_agente|familia|hora|personas
           ----------------------------------------------------- */
        fd = open(pipe_principal, O_WRONLY);

        snprintf(mensaje, sizeof(mensaje),
                 "SOLICITUD|%s|%s|%d|%d",
                 nombre, familia, hora_sol, personas);

        write(fd, mensaje, strlen(mensaje)+1);
        close(fd);

        /* -----------------------------------------------------
           8. ESPERAR LA RESPUESTA DEL CONTROLADOR
           (ACEPTADA, REPROGRAMADA o NEGADA)
           ----------------------------------------------------- */
        read(fd_resp, resp, BUFFER);
        printf("[AGENTE %s] ← %s\n", nombre, resp);

        sleep(2); // pausa pequeña para que no sature el controlador
    }

    /* Cerramos archivo y pipe personal */
    fclose(f);
    close(fd_resp);

    /* Eliminamos nuestro pipe personal */
    unlink(pipe_personal);

    printf("Agente %s termina.\n", nombre);
    return 0;
}
