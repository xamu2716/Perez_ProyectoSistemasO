🛝 Parque_de_Reservas_POSIX  
Sistema de reservas concurrente con hilos, señales y comunicación mediante tuberías FIFO.

---------------------------------------------------------------------
🌟 INTRODUCCIÓN
---------------------------------------------------------------------

Este proyecto implementa un simulador de reservas para un parque de diversiones
como práctica de la materia Sistemas Operativos.

El sistema modela:

- Un controlador que administra la capacidad del parque por horas.
- Múltiples agentes externos que envían solicitudes de reserva.
- Comunicación mediante pipes FIFO.
- Concurrencia mediante hilos POSIX (pthread).
- Avance del tiempo utilizando señales SIGALRM y variables volatile sig_atomic_t.

Conceptos reforzados:
- Creación y manejo de hilos.
- Exclusión mutua con pthread_mutex.
- Comunicación entre procesos con FIFO.
- Señales POSIX y programación del tiempo.
- Parsing de mensajes y control de estados concurrentes.

---------------------------------------------------------------------
📄 DESCRIPCIÓN DEL SISTEMA
---------------------------------------------------------------------

El sistema tiene dos componentes principales:

---------------------------------------------------------------------
1. CONTROLADOR (controlador.c)
---------------------------------------------------------------------

Responsabilidades:
- Administrar el parque entre la hora de inicio y la hora final simulada.
- Recibir y procesar solicitudes de agentes.
- Validar horas, aforo y disponibilidad.
- Llevar estadísticas: aceptadas, reprogramadas, negadas.
- Avanzar la simulación en intervalos programados usando SIGALRM.
- Mantener consistencia en estructuras críticas con mutex.
- Al finalizar la simulación, generar un reporte completo.

Flujo del controlador:
1. Recibe parámetros por línea de comandos:
   - Hora inicial
   - Hora final
   - Segundos por cada hora simulada
   - Aforo máximo permitido
   - Ruta del pipe principal FIFO
2. Crea el FIFO principal.
3. Lanza un hilo dedicado a recibir mensajes.
4. Configura la señal SIGALRM para avanzar la simulación.
5. Mantiene estructuras de reservas con horas y familias.
6. Finaliza con estadísticas y reporte final.

---------------------------------------------------------------------
2. AGENTE (agente.c)
---------------------------------------------------------------------

Cada agente representa una agencia que desea reservar para múltiples familias.

Funciones del agente:
- Crear un FIFO personal para recibir respuestas.
- Registrarse ante el controlador enviando:
  REGISTRO|NombreAgente|RutaPipeAgente
- Recibir la hora actual de simulación tras registrarse.
- Leer un archivo CSV con solicitudes:
  Familia,Hora,Personas
- Enviar una solicitud cada 2 segundos:
  SOLICITUD|Agente|Familia|Hora|Personas
- Imprimir la respuesta del controlador:
  ACEPTADA / REPROGRAMADA / NEGADA

Ejemplo de CSV:
FamiliaLuna,9,4
FamiliaSol,10,3
FamiliaEstrella,11,5

---------------------------------------------------------------------
🔧 REQUISITOS
---------------------------------------------------------------------

- Linux / Unix / WSL
- GCC con pthreads
- mkfifo (FIFO)
- Conocimientos básicos de:
  - C
  - Hilos POSIX
  - Pipes y señales

---------------------------------------------------------------------
🛠 COMPILACIÓN (MAKE)
---------------------------------------------------------------------

Compilar todo:
  make

Limpiar y recompilar:
  make clean
  make

Genera los ejecutables:
  controlador
  agente

---------------------------------------------------------------------
▶️ EJECUCIÓN DEL SISTEMA
---------------------------------------------------------------------

1. Iniciar el controlador:
   ./controlador -i 8 -f 18 -s 5 -t 50 -p /tmp/pipe_principal

2. Iniciar un agente:
   ./agente -s AgenciaFeliz -a reservas.csv -p /tmp/pipe_principal

3. Iniciar múltiples agentes:
   ./agente -s AgenciaUno   -a uno.csv   -p /tmp/pipe_principal &
   ./agente -s AgenciaDos   -a dos.csv   -p /tmp/pipe_principal &

El controlador irá mostrando las horas simuladas y las familias en el parque.

---------------------------------------------------------------------
📊 LÓGICA DE RESERVAS
---------------------------------------------------------------------

- Cada reserva ocupa dos horas consecutivas.
- Validaciones:
  - Hora dentro del rango
  - No reservar una hora ya pasada
  - No exceder el aforo
- Si hay cupo:
    ACEPTADA
- Si no hay cupo pero existe hora disponible:
    REPROGRAMADA
- Si no hay hora disponible:
    NEGADA

---------------------------------------------------------------------
📌 IMPLEMENTACIÓN TÉCNICA
---------------------------------------------------------------------

Tecnologías y funciones usadas:
- Hilos POSIX:
    pthread_create
    pthread_join
- Exclusión mutua:
    pthread_mutex_lock
    pthread_mutex_unlock
- Señales POSIX:
    signal(SIGALRM, handler)
    alarm(segundos)
- FIFO:
    mkfifo
    open / read / write
- Parsing de mensajes con sscanf
- Buffers protegidos y reabrir FIFO cuando read() = 0

---------------------------------------------------------------------
📊 REPORTE FINAL
---------------------------------------------------------------------

El controlador imprime:
- Horas pico (mayor aforo)
- Horas de menor ocupación
- Número de:
    - Solicitudes aceptadas
    - Solicitudes reprogramadas
    - Solicitudes negadas

---------------------------------------------------------------------
🙌 AUTOR
---------------------------------------------------------------------

Xamuel Pérez Madrigal  
Sistemas Operativos – 2025 🐧

---------------------------------------------------------------------
FIN DEL DOCUMENTO
---------------------------------------------------------------------
