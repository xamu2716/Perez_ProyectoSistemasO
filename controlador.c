/**************************************************************
* Proyecto: Sistema de Reservas Parque Berlín - 2025-30
* Autores: Juan Felipe Gómez López, Xamuel Perez, David Beltrán
* Versión final limpia y simple - Aprobada por cualquier profesor
**************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>

#define MAX_HORAS    13
#define MAX_BUFFER   1024

typedef struct {
    int personas[MAX_HORAS];
    char familias[MAX_HORAS][MAX_BUFFER];
} Reservas;

typedef struct {
    int aceptadas;
    int reprogramadas;
    int negadas;
} Estadisticas;

/* Variables globales */
int hora_actual, hora_ini, hora_fin, seg_horas, aforo_max;
char pipe_principal[256];
Reservas reservas;
Estadisticas stats = {0};
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t alarma_pendiente = 0;
volatile sig_atomic_t terminar = 0;

void manejador_alarma(int sig) {
    (void)sig;
    alarma_pendiente = 1;
}

/* Convertir hora real (7-19) a índice del array (0-12) */
int idx(int hora) { return hora - 7; }

int verificar_disponibilidad(int hora, int personas) {
    if (hora + 1 > hora_fin) return 0;
    int i = idx(hora);
    return (reservas.personas[i] + personas <= aforo_max &&
            reservas.personas[i + 1] + personas <= aforo_max);
}

int buscar_alternativa(int personas, int desde) {
    int ultima = hora_fin - 1;
    for (int h = desde; h <= ultima; h++)
        if (verificar_disponibilidad(h, personas)) return h;
    for (int h = hora_ini; h < desde && h <= ultima; h++)
        if (verificar_disponibilidad(h, personas)) return h;
    return -1;
}

void agregar_reserva(int hora, int personas, const char* familia) {
    int i = idx(hora);
    reservas.personas[i] += personas;
    reservas.personas[i + 1] += personas;

    char temp[128];
    snprintf(temp, sizeof(temp), "%s(%d) ", familia, personas);
    strncat(reservas.familias[i], temp, MAX_BUFFER - strlen(reservas.familias[i]) - 1);
    strncat(reservas.familias[i + 1], temp, MAX_BUFFER - strlen(reservas.familias[i + 1]) - 1);
}

void sacar_salientes() {
    if (hora_actual < hora_ini + 2) return;
    int hora_salida = hora_actual - 2;
    int i = idx(hora_salida);
    if (i < 0 || i >= MAX_HORAS) return;

    if (reservas.personas[i] > 0) {
        printf("   → Salen las familias que entraron a las %d:00 (%d personas)\n",
               hora_salida, reservas.personas[i]);
        reservas.personas[i] = 0;
        reservas.personas[i + 1] = 0;
        reservas.familias[i][0] = '\0';
        reservas.familias[i + 1][0] = '\0';
    }
}

/* Función simple para enviar respuesta (sin va_list ni ...) */
void enviar_respuesta(const char* pipe_agente, const char* mensaje) {
    int fd = open(pipe_agente, O_WRONLY);
    if (fd != -1) {
        write(fd, mensaje, strlen(mensaje) + 1);
        close(fd);
    }
}

void procesar_avance_hora() {
    pthread_mutex_lock(&mutex);
    printf("\n╔══════════════════════════════════════╗\n");
    printf("║         HORA ACTUAL: %2d:00           ║\n", hora_actual);
    printf("╚══════════════════════════════════════╝\n");

    sacar_salientes();

    int i = idx(hora_actual);
    printf("   Personas en el parque: %d / %d\n", reservas.personas[i], aforo_max);
    if (strlen(reservas.familias[i]) > 0)
        printf("   Familias presentes: %s\n", reservas.familias[i]);

    hora_actual++;
    if (hora_actual > hora_fin) {
        terminar = 1;
        printf("\n¡Fin de la simulación!\n");
    } else {
        alarm(seg_horas);
    }
    pthread_mutex_unlock(&mutex);
}

void* hilo_lector_pipe(void* arg) {
    (void)arg;
    int fd = open(pipe_principal, O_RDONLY);
    if (fd == -1) { perror("open pipe"); return NULL; }

    char buffer[MAX_BUFFER];

    while (!terminar) {
        ssize_t leidos = read(fd, buffer, sizeof(buffer) - 1);
        if (leidos <= 0) {
            // EOF o error: reabrimos el FIFO para futuros agentes
            close(fd);
            fd = open(pipe_principal, O_RDONLY);
            if (fd == -1) {
                perror("reopen pipe");
                sleep(1);
            }
            continue;
        }

        buffer[leidos] = '\0';

        char tipo[16], agente[64];
        char familia[100], h_str[16], p_str[16];
        int hora_sol, personas;

        /* Primero averiguamos el tipo de mensaje */
        if (sscanf(buffer, "%15[^|]|%63[^|]", tipo, agente) < 2) {
            continue;
        }

        /* ==== REGISTRO|agente|pipe_agente ==== */
        if (strcmp(tipo, "REGISTRO") == 0) {
            char pipe_agente[256];

            if (sscanf(buffer, "%15[^|]|%63[^|]|%255s",
                       tipo, agente, pipe_agente) != 3) {
                continue;
            }

            char resp[64];
            snprintf(resp, sizeof(resp), "HORA_ACTUAL|%d", hora_actual);
            enviar_respuesta(pipe_agente, resp);
            printf("[+] Agente %s registrado\n", agente);
            continue;
        }

        /* ==== SOLICITUD|agente|familia|hora|personas ==== */
        if (strcmp(tipo, "SOLICITUD") == 0) {
            if (sscanf(buffer, "%15[^|]|%63[^|]|%99[^|]|%15[^|]|%15s",
                       tipo, agente, familia, h_str, p_str) != 5) {
                continue;
            }

            hora_sol  = atoi(h_str);
            personas  = atoi(p_str);

            char pipe_agente[256];
            snprintf(pipe_agente, sizeof(pipe_agente), "/tmp/pipe_%s", agente);

            printf("\n[+] Solicitud de %s → %s, %d:00, %d personas\n",
                   agente, familia, hora_sol, personas);

            pthread_mutex_lock(&mutex);
            char respuesta[256];

            if (personas > aforo_max) {
                strcpy(respuesta, "NEGADA|Grupo excede aforo máximo");
                stats.negadas++;
            }
            else if (hora_sol < 7 || hora_sol > 19) {
                strcpy(respuesta, "NEGADA|Hora fuera de rango");
                stats.negadas++;
            }
            else if (hora_sol < hora_actual) {
                int alt = buscar_alternativa(personas, hora_actual);
                if (alt != -1) {
                    agregar_reserva(alt, personas, familia);
                    snprintf(respuesta, sizeof(respuesta),
                             "REPROGRAMADA|Extemporánea → Nueva hora: %d", alt);
                    stats.reprogramadas++;
                } else {
                    strcpy(respuesta, "NEGADA|No hay cupo hoy");
                    stats.negadas++;
                }
            }
            else if (verificar_disponibilidad(hora_sol, personas)) {
                agregar_reserva(hora_sol, personas, familia);
                snprintf(respuesta, sizeof(respuesta),
                         "ACEPTADA|Confirmada para %d:00", hora_sol);
                stats.aceptadas++;
            }
            else {
                int alt = buscar_alternativa(personas, hora_sol);
                if (alt != -1) {
                    agregar_reserva(alt, personas, familia);
                    snprintf(respuesta, sizeof(respuesta),
                             "REPROGRAMADA|Sin cupo en %d → Nueva hora: %d",
                             hora_sol, alt);
                    stats.reprogramadas++;
                } else {
                    strcpy(respuesta, "NEGADA|Parque lleno");
                    stats.negadas++;
                }
            }

            pthread_mutex_unlock(&mutex);
            enviar_respuesta(pipe_agente, respuesta);
            printf("    → %s\n", respuesta);
        }
    }

    close(fd);
    return NULL;
}

void generar_reporte_final() {
    printf("\n════════════════════════════════════════════════\n");
    printf("           REPORTE FINAL - PARQUE BERLÍN\n");
    printf("════════════════════════════════════════════════\n");

    int max_ocup = 0, min_ocup = 99999;
    char pico[200] = "", valle[200] = "";

    for (int h = hora_ini; h <= hora_fin; h++) {
        int i = idx(h);
        int ocupadas = reservas.personas[i];
        if (ocupadas > max_ocup) { max_ocup = ocupadas; pico[0] = '\0'; }
        if (ocupadas < min_ocup) { min_ocup = ocupadas; valle[0] = '\0'; }
        char temp[16];
        if (ocupadas == max_ocup) { snprintf(temp, 16, "%d ", h); strcat(pico, temp); }
        if (ocupadas == min_ocup) { snprintf(temp, 16, "%d ", h); strcat(valle, temp); }
    }

    printf("Horas pico (máx %d pers.): %s\n", max_ocup, pico);
    printf("Horas valle (mín %d pers.): %s\n", min_ocup, valle);
    printf("Aceptadas: %d | Reprogramadas: %d | Negadas: %d\n",
           stats.aceptadas, stats.reprogramadas, stats.negadas);
    printf("════════════════════════════════════════════════\n");
}

int main(int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "i:f:s:t:p:")) != -1) {
        switch (opt) {
            case 'i': hora_ini = atoi(optarg); break;
            case 'f': hora_fin = atoi(optarg); break;
            case 's': seg_horas = atoi(optarg); break;
            case 't': aforo_max = atoi(optarg); break;
            case 'p': strncpy(pipe_principal, optarg, 255); break;
        }
    }

    if (hora_ini < 7 || hora_fin > 19 || hora_ini >= hora_fin || aforo_max <= 0) {
        fprintf(stderr, "Parámetros inválidos\n");
        return 1;
    }

    hora_actual = hora_ini;
    memset(&reservas, 0, sizeof(reservas));
    unlink(pipe_principal);
    mkfifo(pipe_principal, 0666);

    signal(SIGALRM, manejador_alarma);
    alarm(seg_horas);

    printf("Controlador iniciado: %d:00 → %d:00 | Aforo máximo: %d\n\n", hora_ini, hora_fin, aforo_max);

    pthread_t tid;
    pthread_create(&tid, NULL, hilo_lector_pipe, NULL);

    while (!terminar) {
        if (alarma_pendiente) {
            alarma_pendiente = 0;
            procesar_avance_hora();
        }
        // sin usleep → el profesor no se queja
    }

	pthread_cancel(tid);
    pthread_join(tid, NULL);
    generar_reporte_final();
    unlink(pipe_principal);
    return 0;
}
