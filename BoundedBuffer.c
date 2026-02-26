/*
 * ----------------------------------------------------------------------------------------------------------------------------------------------------------------
 * BoundedBuffer.c
 * ----------------------------------------------------------------------------------------------------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Sistemas Operativos
 *
 * Descripción: Programa que simula el problema de sincronización de Bounded Buffer mediante un escenario de supermercado con hilos concurrentes.
 *
 *              Implementa la coordinación entre cajeros y empacadores para el manejo de un área de almacenamiento compartido. El sistema utiliza semáforos 
 *              manuales construidos con exclusión mutua y variables de condición para gestionar el bloqueo de procesos cuando el espacio está lleno o 
 *              vacío. Se garantiza la integridad de los datos en la sección crítica y se previene el deadlock mediante señales de control sincronizadas.
 *
 * Autores:     André Pivaral, Ángel Mérida y José Sánchez 
 * Fecha:       20 de Febrero de 2026
 * ----------------------------------------------------------------------------------------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

/* -------------------- PARÁMETROS CONFIGURABLES -------------------- */
#define BUFFER_SIZE      5
#define NUM_CAJEROS      3
#define NUM_EMPACADORES  2
#define DURACION_SEG    60

/* -------------------- IMPLEMENTACIÓN SEMÁFORO -------------------- */
/**
 * Estructura de datos para implementar un semáforo manual
 * Utiliza un mutex y una variable de condición para sincronización
 * 
 * value: Contador del semáforo (número de recursos disponibles)
 * mtx:   Mutex para proteger el acceso a 'value'
 * cond:  Variable de condición para bloquear/despertar hilos
 */
typedef struct {
    int             value;
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
} Semaforo;

/**
 * Inicializa un semáforo con un valor inicial
 * 
 * Parámetros:
 *   s:     Puntero al semáforo a inicializar
 *   valor: Valor inicial del contador (número de recursos disponibles)
 * 
 * Inicializa el mutex y la variable de condición necesarios para
 * implementar las operaciones wait y signal del semáforo.
 */
void sem_inicializar(Semaforo *s, int valor)
{
    s->value = valor;
    pthread_mutex_init(&s->mtx, NULL);
    pthread_cond_init(&s->cond, NULL);
}

/**
 * Operación WAIT del semáforo (también conocida como P o down)
 * 
 * Parámetros:
 *   s: Puntero al semáforo
 * 
 * Decrementa el contador del semáforo. Si el resultado es negativo,
 * el hilo se bloquea esperando que otro hilo haga signal.
 * 
 * Implementación:
 *   1. Adquiere el mutex para proteger la sección crítica
 *   2. Decrementa el contador
 *   3. Si el contador es negativo, el hilo se duerme en la variable de condición
 *   4. Libera el mutex al salir (o automáticamente si se bloquea)
 */
void sem_wait_manual(Semaforo *s)
{
    pthread_mutex_lock(&s->mtx);        // Entra a sección crítica
    s->value--;                         // Decrementa recurso
    if (s->value < 0) {
        pthread_cond_wait(&s->cond, &s->mtx);  // Bloquea si no hay recursos
    }
    pthread_mutex_unlock(&s->mtx);      // Sale de sección crítica
}

/**
 * Operación SIGNAL del semáforo (también conocida como V o up)
 * 
 * Parámetros:
 *   s: Puntero al semáforo
 * 
 * Incrementa el contador del semáforo. Si hay hilos esperando
 * (contador <= 0), despierta a uno de ellos.
 * 
 * Implementación:
 *   1. Adquiere el mutex para proteger la sección crítica
 *   2. Incrementa el contador
 *   3. Si el contador es <= 0, hay hilos esperando, despierta uno
 *   4. Libera el mutex
 */
void sem_signal_manual(Semaforo *s)
{
    pthread_mutex_lock(&s->mtx);        // Entra a sección crítica
    s->value++;                         // Incrementa recurso
    if (s->value <= 0) {
        pthread_cond_signal(&s->cond);  // Despierta un hilo esperando
    }
    pthread_mutex_unlock(&s->mtx);      // Sale de sección crítica
}

/**
 * Destruye un semáforo liberando sus recursos
 * 
 * Parámetros:
 *   s: Puntero al semáforo a destruir
 * 
 * Libera el mutex y la variable de condición asociados al semáforo.
 * Debe llamarse cuando el semáforo ya no sea necesario.
 */
void sem_destruir(Semaforo *s)
{
    pthread_mutex_destroy(&s->mtx);
    pthread_cond_destroy(&s->cond);
}

/* -------------------- PRODUCTOS SUPERMERCADO -------------------- */
static const char *productos[] = {
    "Leche", "Pan", "Huevos", "Cereal", "Manzanas",
    "Agua",  "Arroz","Frijoles","Jugo",  "Galletas"
};
#define NUM_PRODUCTOS (int)(sizeof(productos)/sizeof(productos[0]))

/* -------------------- BUFFER COMPARTIDO -------------------- */
/**
 * Estructura que representa un producto del supermercado
 * 
 * nombre: Nombre del producto (ej: "Leche", "Pan")
 * codigo: Código único del producto para identificación
 */
typedef struct {
    char nombre[32];
    int  codigo;
} Producto;

// Buffer circular compartido entre cajeros y empacadores
Producto area_empaque[BUFFER_SIZE];  // Área donde se colocan productos escaneados
int      indice_in   = 0;             // Índice donde el productor inserta (cajero)
int      indice_out  = 0;             // Índice donde el consumidor extrae (empacador)
int      total_producidos = 0;        // Contador total de productos escaneados
int      total_consumidos = 0;        // Contador total de productos empacados

/* -------------------- PRIMITIVAS SINCRONIZACIÓN -------------------- */

// Semáforo que cuenta espacios vacíos en el buffer (inicia con BUFFER_SIZE)
Semaforo        sem_empty;

// Semáforo que cuenta espacios llenos en el buffer (inicia con 0)
Semaforo        sem_full;

// Mutex para proteger el acceso al buffer compartido (sección crítica)
pthread_mutex_t mutex;

/* -------------------- CONTROL TIEMPO -------------------- */
// Bandera global para controlar la duración de la simulación
// volatile asegura que el compilador no optimice su lectura
volatile int simulacion_activa = 1;

/* -------------------- UTILIDADES LOG -------------------- */
/**
 * Imprime un evento de la simulación con formato consistente y timestamp
 * 
 * Parámetros:
 *   rol:      Tipo de hilo ("CAJERO" o "EMPACADOR")
 *   id:       Número identificador del hilo
 *   accion:   Descripción de la acción realizada
 *   producto: Nombre del producto involucrado
 *   ocupados: Número actual de espacios ocupados en el buffer
 * 
 * Formato de salida:
 *   [HH:MM:SS] ROL #ID | ACCIÓN | Producto: NOMBRE | Buffer: X/Y
 */
void log_evento(const char *rol, int id, const char *accion,
                const char *producto, int ocupados)
{
    time_t    t  = time(NULL);
    struct tm *tm = localtime(&t);
    printf("[%02d:%02d:%02d] %-10s #%d | %-35s | Producto: %-10s | Buffer: %d/%d\n",
           tm->tm_hour, tm->tm_min, tm->tm_sec,
           rol, id, accion, producto, ocupados, BUFFER_SIZE);
    fflush(stdout);  // Asegura que el mensaje se imprima inmediatamente
}

/**
 * Calcula el número de espacios ocupados en el buffer circular
 * 
 * Retorna:
 *   Número de productos actualmente en el buffer (0 a BUFFER_SIZE)
 * 
 * La fórmula maneja correctamente el caso cuando indice_in < indice_out
 * debido a que el buffer es circular.
 */
int buffer_ocupados(void)
{
    return (indice_in - indice_out + BUFFER_SIZE) % BUFFER_SIZE;
}

/* -------------------- HILO: CAJERO - PRODUCTOR -------------------- */
/**
 * Función ejecutada por cada hilo cajero (productor)
 * 
 * Parámetros:
 *   arg: Puntero a un entero con el ID del cajero
 * 
 * Comportamiento:
 *   1. Simula el escaneo de productos con un delay aleatorio
 *   2. Coloca productos en el área de empaque (buffer compartido)
 *   3. Utiliza semáforos para coordinar con empacadores
 *   4. Se ejecuta hasta que simulacion_activa = 0
 * 
 * Protocolo de sincronización:
 *   - sem_wait(empty): Espera que haya espacio disponible
 *   - mutex_lock: Entra a sección crítica para modificar el buffer
 *   - mutex_unlock: Sale de sección crítica
 *   - sem_signal(full): Señala que hay un producto disponible
 */
void *cajero(void *arg)
{
    int id = *((int *)arg);
    free(arg);

    // Inicializa semilla aleatoria única para este cajero
    srand((unsigned int)(time(NULL)) ^ (unsigned int)(id * 1234));

    while (simulacion_activa) {

        // Simula tiempo de escaneo de producto (200-1000 ms)
        usleep((rand() % 800 + 200) * 1000);

        if (!simulacion_activa) break;

        // Crea un nuevo producto con datos aleatorios
        Producto p;
        p.codigo = rand() % 9000 + 1000;  // Código entre 1000-9999
        strncpy(p.nombre, productos[rand() % NUM_PRODUCTOS],
                sizeof(p.nombre) - 1);
        p.nombre[sizeof(p.nombre) - 1] = '\0';  // Asegura terminación nula

        // WAIT en sem_empty: espera que haya espacio en el buffer
        sem_wait_manual(&sem_empty);

        // Verifica si se debe terminar; libera semáforo para no bloquear otros
        if (!simulacion_activa) { sem_signal_manual(&sem_empty); break; }

        // ===== INICIA SECCIÓN CRÍTICA =====
        pthread_mutex_lock(&mutex);

        // Coloca el producto en el buffer circular
        area_empaque[indice_in] = p;
        indice_in = (indice_in + 1) % BUFFER_SIZE;  // Avanza índice circularmente
        total_producidos++;
        int ocupados = buffer_ocupados();

        log_evento("CAJERO", id, "ENTRA SC - coloca producto",
                   p.nombre, ocupados);

        pthread_mutex_unlock(&mutex);
        // ===== FIN SECCIÓN CRÍTICA =====

        log_evento("CAJERO", id, "SALE  SC",
                   p.nombre, ocupados);

        // SIGNAL en sem_full: indica que hay un producto disponible
        sem_signal_manual(&sem_full);
    }

    printf("[FIN] Cajero     #%d termino.\n", id);
    return NULL;
}

/* -------------------- HILO: EMPACADOR - CONSUMIDOR -------------------- */
/**
 * Función ejecutada por cada hilo empacador (consumidor)
 * 
 * Parámetros:
 *   arg: Puntero a un entero con el ID del empacador
 * 
 * Comportamiento:
 *   1. Toma productos del área de empaque (buffer compartido)
 *   2. Simula el empacado con un delay aleatorio
 *   3. Utiliza semáforos para coordinar con cajeros
 *   4. Se ejecuta hasta que simulacion_activa = 0
 * 
 * Protocolo de sincronización:
 *   - sem_wait(full): Espera que haya un producto disponible
 *   - mutex_lock: Entra a sección crítica para modificar el buffer
 *   - mutex_unlock: Sale de sección crítica
 *   - sem_signal(empty): Señala que hay un espacio libre
 */
void *empacador(void *arg)
{
    int id = *((int *)arg);
    free(arg);

    // Inicializa semilla aleatoria única para este empacador
    srand((unsigned int)(time(NULL)) ^ (unsigned int)(id * 5678));

    while (simulacion_activa) {

        // WAIT en sem_full: espera que haya un producto en el buffer
        sem_wait_manual(&sem_full);

        // Verifica si se debe terminar; libera semáforo para no bloquear otros
        if (!simulacion_activa) { sem_signal_manual(&sem_full); break; }

        // ===== INICIA SECCIÓN CRÍTICA =====
        pthread_mutex_lock(&mutex);

        // Toma el producto del buffer circular
        Producto p   = area_empaque[indice_out];
        indice_out   = (indice_out + 1) % BUFFER_SIZE;  // Avanza índice circularmente
        total_consumidos++;
        int ocupados = buffer_ocupados();

        log_evento("EMPACADOR", id, "ENTRA SC - toma producto",
                   p.nombre, ocupados);

        pthread_mutex_unlock(&mutex);
        // ===== FIN SECCIÓN CRÍTICA =====

        log_evento("EMPACADOR", id, "SALE  SC",
                   p.nombre, ocupados);

        // SIGNAL en sem_empty: indica que hay un espacio libre
        sem_signal_manual(&sem_empty);

        // Simula tiempo de empacado (400-1600 ms)
        usleep((rand() % 1200 + 400) * 1000);
    }

    printf("[FIN] Empacador  #%d termino.\n", id);
    return NULL;
}

/* -------------------- HILO: TEMPORIZADOR -------------------- */
/**
 * Función ejecutada por el hilo temporizador
 * 

 * 
 * Comportamiento:
 *   1. Espera por DURACION_SEG segundos
 *   2. Establece simulacion_activa = 0 para detener todos los hilos
 *   3. Envía señales a los semáforos para despertar hilos bloqueados
 *      y permitirles terminar correctamente
 *
 * Propósito:
 *   Controla la duración de la simulación y asegura una terminación
 *   limpia de todos los hilos sin deadlock.
 */
void *temporizador(void *arg)
{
    (void)arg;  // Suprime warning de parámetro no usado
    sleep(DURACION_SEG);  // Espera el tiempo de simulación
    simulacion_activa = 0;  // Señala a todos los hilos que deben terminar

    // Despierta todos los hilos que puedan estar bloqueados en semáforos
    // para que puedan verificar simulacion_activa y terminar
    int i;
    for (i = 0; i < NUM_CAJEROS + NUM_EMPACADORES; i++) {
        sem_signal_manual(&sem_full);   // Despierta empacadores
        sem_signal_manual(&sem_empty);  // Despierta cajeros
    }
    return NULL;
}


int main(void)
{
    int i;
    pthread_t hilos_cajero[NUM_CAJEROS];       // Array de hilos cajeros
    pthread_t hilos_empacador[NUM_EMPACADORES]; // Array de hilos empacadores
    pthread_t hilo_timer;                       // Hilo temporizador

    // ===== IMPRIME ENCABEZADO DE LA SIMULACIÓN =====
    printf("--------------------------------------------------------------------------------\n");
    printf("                    SISTEMAS OPERATIVOS - LABBORATORIO 2.2\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("    Bounded Buffer - Semáforos + Mutex\n");
    printf("    Simulación Supermercado\n\n");
    printf("    Buffer - Área Empaque: %d Productos\n", BUFFER_SIZE);
    printf("    Cajeros - Productores: %d\n", NUM_CAJEROS);
    printf("    Empacadores - Consumidores: %d\n\n", NUM_EMPACADORES);
    printf("    Duración Simulación: %d Segundos\n", DURACION_SEG);
    printf("--------------------------------------------------------------------------------\n");

    // ===== INICIALIZA PRIMITIVAS DE SINCRONIZACIÓN =====
    // sem_empty: inicializa con BUFFER_SIZE (todos los espacios vacíos)
    sem_inicializar(&sem_empty, BUFFER_SIZE);
    // sem_full: inicializa con 0 (ningún producto disponible)
    sem_inicializar(&sem_full,  0);
    // mutex: para proteger acceso al buffer compartido
    pthread_mutex_init(&mutex, NULL);

    // ===== CREA HILOS =====
    // Crea hilo temporizador que controlará la duración
    pthread_create(&hilo_timer, NULL, temporizador, NULL);

    // Crea hilos cajeros (productores)
    for (i = 0; i < NUM_CAJEROS; i++) {
        int *id = malloc(sizeof(int));  // Asigna memoria para ID único
        *id = i + 1;                    // ID comienza en 1
        pthread_create(&hilos_cajero[i], NULL, cajero, id);
    }

    // Crea hilos empacadores (consumidores)
    for (i = 0; i < NUM_EMPACADORES; i++) {
        int *id = malloc(sizeof(int));  // Asigna memoria para ID único
        *id = i + 1;                    // ID comienza en 1
        pthread_create(&hilos_empacador[i], NULL, empacador, id);
    }

    // ===== ESPERA A QUE TODOS LOS HILOS TERMINEN =====
    // Primero espera al temporizador (controla la duración)
    pthread_join(hilo_timer, NULL);
    // Luego espera a que todos los cajeros terminen
    for (i = 0; i < NUM_CAJEROS;     i++) pthread_join(hilos_cajero[i],    NULL);
    // Finalmente espera a que todos los empacadores terminen
    for (i = 0; i < NUM_EMPACADORES; i++) pthread_join(hilos_empacador[i], NULL);

    // ===== IMPRIME ESTADÍSTICAS FINALES =====
    printf("--------------------------------------------------------------------------------\n");
    printf("                                FIN SIMULACIÓN \n");
    printf("--------------------------------------------------------------------------------\n");
    printf("  Productos Escaneados - Producidos: %d\n", total_producidos);
    printf("  Productos Empacados - consumidos: %d\n", total_consumidos);
    printf("  Productos en el Área de Empaque en el Fin: %d\n", total_producidos - total_consumidos);
    printf("--------------------------------------------------------------------------------\n");

    // ===== LIMPIA RECURSOS =====
    // Destruye las primitivas de sincronización para liberar recursos
    sem_destruir(&sem_empty);
    sem_destruir(&sem_full);
    pthread_mutex_destroy(&mutex);

    return 0;
}