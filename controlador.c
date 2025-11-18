/**************************************************************
* Proyecto: Sistema de Reservas – Parque Berlín (2025-30)
* Autores: Juan Felipe Gómez López, Xamuel Pérez, David Beltrán
*
* Notas del estudiante:
*   Este archivo implementa el Controlador. Es básicamente el
*   “servidor” del sistema: recibe agentes, procesa solicitudes
*   y avanza la simulación del parque hora por hora.
*
*   Se usan:
*     - Pipes FIFO → comunicación entre procesos.
*     - Hilos POSIX → para escuchar agentes en paralelo.
*     - Señal SIGALRM → para avanzar la hora automáticamente.
*     - Mutex → para evitar condiciones de carrera mientras
*               varios agentes envían solicitudes al mismo tiempo.
**************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>

#define MAX_HORAS    13     // 7AM–19PM → 13 horas posibles
#define MAX_BUFFER   1024   // buffer general para leer mensajes

/* Estructura donde guardamos cuántas personas hay por hora
   y también los nombres de las familias que están registradas. */
typedef struct {
    int personas[MAX_HORAS];
    char familias[MAX_HORAS][MAX_BUFFER];
} Reservas;

/* Llevamos estadísticas simples: aceptadas, reprogramadas y negadas */
typedef struct {
    int aceptadas;
    int reprogramadas;
    int negadas;
} Estadisticas;

/* ====== Variables globales ====== */
int hora_actual, hora_ini, hora_fin, seg_horas, aforo_max;
char pipe_principal[256];
Reservas reservas;
Estadisticas stats = {0};  // inicializa todo a cero

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* Variables para la señal SIGALRM */
volatile sig_atomic_t alarma_pendiente = 0;  // marca cuando ocurre una alarma
volatile sig_atomic_t terminar = 0;          // indica fin de simulación

/* Manejador de la señal SIGALRM → solo marca que hay que avanzar la hora */
void manejador_alarma(int sig) {
    (void)sig;               // evita warning
    alarma_pendiente = 1;    // indica al main que debe avanzar
}

/* Conversión de hora real (7–19) a índice 0–12 */
int idx(int hora) { return hora - 7; }

/* Verifica si en las dos horas consecutivas hay espacio suficiente.
   Cada reserva dura exactamente dos horas. */
int verificar_disponibilidad(int hora, int personas) {
    if (hora + 1 > hora_fin) return 0;  // no cabe en la última hora
    int i = idx(hora);
    return (reservas.personas[i] + personas <= aforo_max &&
            reservas.personas[i + 1] + personas <= aforo_max);
}

/* Busca la primera hora alternativa con espacio cuando la original falla */
int buscar_alternativa(int personas, int desde) {
    int ultima = hora_fin - 1;

    /* Buscar hacia adelante */
    for (int h = desde; h <= ultima; h++)
        if (verificar_disponibilidad(h, personas)) return h;

    /* Buscar hacia atrás si no se encontró antes */
    for (int h = hora_ini; h < desde && h <= ultima; h++)
        if (verificar_disponibilidad(h, personas)) return h;

    return -1;  // no hay cupo en ninguna hora
}

/* Agrega una reserva en dos horas consecutivas */
void agregar_reserva(int hora, int personas, const char* familia) {
    int i = idx(hora);

    reservas.personas[i]     += personas;
    reservas.personas[i + 1] += personas;

    /* Guardamos familia y número de personas para mostrar luego */
    char temp[128];
    snprintf(temp, sizeof(temp), "%s(%d) ", familia, personas);

    strncat(reservas.familias[i],     temp,
            MAX_BUFFER - strlen(reservas.familias[i]) - 1);

    strncat(reservas.familias[i + 1], temp,
            MAX_BUFFER - strlen(reservas.familias[i + 1]) - 1);
}

/* Elimina grupos que ya cumplieron sus dos horas */
void sacar_salientes() {
    if (hora_actual < hora_ini + 2) return;  // antes de hora 9 no sale nadie

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

/* Enviar respuesta al agente usando su pipe */
void enviar_respuesta(const char* pipe_agente, const char* mensaje) {
    int fd = open(pipe_agente, O_WRONLY);
    if (fd != -1) {
        write(fd, mensaje, strlen(mensaje) + 1); // enviamos también '\0'
        close(fd);
    }
}

/* Avanzar la hora cuando llega SIGALRM */
void procesar_avance_hora() {
    pthread_mutex_lock(&mutex);

    printf("\n╔══════════════════════════════════════╗\n");
    printf("║         HORA ACTUAL: %2d:00           ║\n", hora_actual);
    printf("╚══════════════════════════════════════╝\n");

    sacar_salientes(); // limpia los grupos que ya cumplieron sus 2 horas

    int i = idx(hora_actual);
    printf("   Personas en el parque: %d / %d\n",
           reservas.personas[i], aforo_max);

    if (strlen(reservas.familias[i]) > 0)
        printf("   Familias presentes: %s\n", reservas.familias[i]);

    hora_actual++;

    /* Si ya pasamos la hora fin → todo termina */
    if (hora_actual > hora_fin) {
        terminar = 1;
        printf("\n¡Fin de la simulación!\n");
    } else {
        alarm(seg_horas);   // programamos siguiente avance
    }

    pthread_mutex_unlock(&mutex);
}

/* ================================================================
   HILO: escucha mensajes en el pipe principal durante toda la simulación.
   Aquí se procesan los REGISTRO y las SOLICITUD.
   ================================================================ */
void* hilo_lector_pipe(void* arg) {
    (void)arg;

    int fd = open(pipe_principal, O_RDONLY);
    if (fd == -1) { perror("open pipe"); return NULL; }

    char buffer[MAX_BUFFER];

    while (!terminar) {

        ssize_t leidos = read(fd, buffer, sizeof(buffer) - 1);

        if (leidos <= 0) {
            /* El FIFO se queda sin escritores → reabrimos */
            close(fd);
            fd = open(pipe_principal, O_RDONLY);

            if (fd == -1) {
                perror("reopen pipe");
                sleep(1);
            }
            continue;
        }

        buffer[leidos] = '\0';  // terminamos el string

        char tipo[16], agente[64];
        char familia[100], h_str[16], p_str[16];
        int hora_sol, personas;

        /* Leemos el tipo del mensaje y el nombre del agente */
        if (sscanf(buffer, "%15[^|]|%63[^|]", tipo, agente) < 2) {
            continue; // mensaje mal formado
        }

        /* ========================== MANEJO DEL REGISTRO ========================== */
        if (strcmp(tipo, "REGISTRO") == 0) {
            char pipe_agente[256];

            if (sscanf(buffer, "%15[^|]|%63[^|]|%255s",
                       tipo, agente, pipe_agente) != 3) {
                continue;
            }

            /* Enviar la hora actual al agente recién registrado */
            char resp[64];
            snprintf(resp, sizeof(resp), "HORA_ACTUAL|%d", hora_actual);

            enviar_respuesta(pipe_agente, resp);

            printf("[+] Agente %s registrado\n", agente);
            continue;
        }

        /* ========================== MANEJO DE SOLICITUD ========================== */
        if (strcmp(tipo, "SOLICITUD") == 0) {

            /* Parseo completo de la solicitud */
            if (sscanf(buffer, "%15[^|]|%63[^|]|%99[^|]|%15[^|]|%15s",
                       tipo, agente, familia, h_str, p_str) != 5) {
                continue;
            }

            hora_sol  = atoi(h_str);
            personas  = atoi(p_str);

            /* Armamos el pipe personal del agente */
            char pipe_agente[256];
            snprintf(pipe_agente, sizeof(pipe_agente),
                     "/tmp/pipe_%s", agente);

            printf("\n[+] Solicitud de %s → %s, %d:00, %d personas\n",
                   agente, familia, hora_sol, personas);

            pthread_mutex_lock(&mutex);

            char respuesta[256];

            /* Validación del aforo y rango de horas */
            if (personas > aforo_max) {
                strcpy(respuesta, "NEGADA|Grupo excede aforo máximo");
                stats.negadas++;
            }
            else if (hora_sol < 7 || hora_sol > 19) {
                strcpy(respuesta, "NEGADA|Hora fuera de rango");
                stats.negadas++;
            }
            else if (hora_sol < hora_actual) {
                /* Extemporánea → buscar otra hora */
                int alt = buscar_alternativa(personas, hora_actual);
                if (alt != -1) {
                    agregar_reserva(alt, personas, familia);
                    snprintf(respuesta, sizeof(respuesta),
                             "REPROGRAMADA|Extemporánea → Nueva hora: %d",
                             alt);
                    stats.reprogramadas++;
                } else {
                    strcpy(respuesta, "NEGADA|No hay cupo hoy");
                    stats.negadas++;
                }
            }
            else if (verificar_disponibilidad(hora_sol, personas)) {
                /* Reserva normal */
                agregar_reserva(hora_sol, personas, familia);
                snprintf(respuesta, sizeof(respuesta),
                         "ACEPTADA|Confirmada para %d:00", hora_sol);
                stats.aceptadas++;
            }
            else {
                /* Sin cupo → buscar alternativa */
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

/* Imprime estadísticas finales al terminar la simulación */
void generar_reporte_final() {
    printf("\n════════════════════════════════════════════════\n");
    printf("           REPORTE FINAL - PARQUE BERLÍN\n");
    printf("════════════════════════════════════════════════\n");

    int max_ocup = 0, min_ocup = 99999;
    char pico[200] = "", valle[200] = "";

    /* Buscamos horas pico y valle según ocupación */
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

/* =========================== MAIN =========================== */
int main(int argc, char* argv[]) {

    /* Leer parámetros de ejecución */
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

    /* Validación sencilla de los parámetros */
    if (hora_ini < 7 || hora_fin > 19 ||
        hora_ini >= hora_fin || aforo_max <= 0) {
        fprintf(stderr, "Parámetros inválidos\n");
        return 1;
    }

    hora_actual = hora_ini;

    memset(&reservas, 0, sizeof(reservas));

    /* Crear FIFO principal del controlador */
    unlink(pipe_principal);
    mkfifo(pipe_principal, 0666);

    /* Configurar señal de alarma */
    signal(SIGALRM, manejador_alarma);
    alarm(seg_horas);

    printf("Controlador iniciado: %d:00 → %d:00 | Aforo máximo: %d\n\n",
           hora_ini, hora_fin, aforo_max);

    /* Crear hilo lector (recibe solicitudes) */
    pthread_t tid;
    pthread_create(&tid, NULL, hilo_lector_pipe, NULL);

    /* Bucle principal: avanza el reloj cuando llega la alarma */
    while (!terminar) {
        if (alarma_pendiente) {
            alarma_pendiente = 0;
            procesar_avance_hora();
        }
    }

    /* Finalización */
    pthread_cancel(tid);
    pthread_join(tid, NULL);

    generar_reporte_final();
    generar_reporte_final();
    generar_reporte_final();
    unlink(pipe_principal);

    return 0;
}
