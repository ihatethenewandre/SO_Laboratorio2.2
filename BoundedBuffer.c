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
typedef struct {
    int             value;
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
} Semaforo;

void sem_inicializar(Semaforo *s, int valor)
{
    s->value = valor;
    pthread_mutex_init(&s->mtx, NULL);
    pthread_cond_init(&s->cond, NULL);
}

void sem_wait_manual(Semaforo *s)
{
    pthread_mutex_lock(&s->mtx);
    s->value--;
    if (s->value < 0) {
        pthread_cond_wait(&s->cond, &s->mtx);
    }
    pthread_mutex_unlock(&s->mtx);
}

void sem_signal_manual(Semaforo *s)
{
    pthread_mutex_lock(&s->mtx);
    s->value++;
    if (s->value <= 0) {
        pthread_cond_signal(&s->cond);
    }
    pthread_mutex_unlock(&s->mtx);
}

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
typedef struct {
    char nombre[32];
    int  codigo;
} Producto;

Producto area_empaque[BUFFER_SIZE];
int      indice_in   = 0;
int      indice_out  = 0; 
int      total_producidos = 0;
int      total_consumidos = 0;

/* -------------------- PRIMITIVAS SINCRONIZACIÓN -------------------- */

Semaforo        sem_empty;
Semaforo        sem_full;
pthread_mutex_t mutex;

/* -------------------- CONTROL TIEMPO -------------------- */
volatile int simulacion_activa = 1;

/* -------------------- UTILIDADES LOG -------------------- */
void log_evento(const char *rol, int id, const char *accion,
                const char *producto, int ocupados)
{
    time_t    t  = time(NULL);
    struct tm *tm = localtime(&t);
    printf("[%02d:%02d:%02d] %-10s #%d | %-35s | Producto: %-10s | Buffer: %d/%d\n",
           tm->tm_hour, tm->tm_min, tm->tm_sec,
           rol, id, accion, producto, ocupados, BUFFER_SIZE);
    fflush(stdout);
}

int buffer_ocupados(void)
{
    return (indice_in - indice_out + BUFFER_SIZE) % BUFFER_SIZE;
}

/* -------------------- HILO: CAJERO - PRODUCTOR -------------------- */
void *cajero(void *arg)
{
    int id = *((int *)arg);
    free(arg);

    srand((unsigned int)(time(NULL)) ^ (unsigned int)(id * 1234));

    while (simulacion_activa) {

        usleep((rand() % 800 + 200) * 1000);

        if (!simulacion_activa) break;

        Producto p;
        p.codigo = rand() % 9000 + 1000;
        strncpy(p.nombre, productos[rand() % NUM_PRODUCTOS],
                sizeof(p.nombre) - 1);
        p.nombre[sizeof(p.nombre) - 1] = '\0';

        sem_wait_manual(&sem_empty);

        if (!simulacion_activa) { sem_signal_manual(&sem_empty); break; }

        pthread_mutex_lock(&mutex);

        area_empaque[indice_in] = p;
        indice_in = (indice_in + 1) % BUFFER_SIZE;
        total_producidos++;
        int ocupados = buffer_ocupados();

        log_evento("CAJERO", id, "ENTRA SC - coloca producto",
                   p.nombre, ocupados);

        pthread_mutex_unlock(&mutex);

        log_evento("CAJERO", id, "SALE  SC",
                   p.nombre, ocupados);

        sem_signal_manual(&sem_full);
    }

    printf("[FIN] Cajero     #%d termino.\n", id);
    return NULL;
}

/* -------------------- HILO: EMPACADOR - CONSUMIDOR -------------------- */
void *empacador(void *arg)
{
    int id = *((int *)arg);
    free(arg);

    srand((unsigned int)(time(NULL)) ^ (unsigned int)(id * 5678));

    while (simulacion_activa) {

        sem_wait_manual(&sem_full);

        if (!simulacion_activa) { sem_signal_manual(&sem_full); break; }

        pthread_mutex_lock(&mutex);

        Producto p   = area_empaque[indice_out];
        indice_out   = (indice_out + 1) % BUFFER_SIZE;
        total_consumidos++;
        int ocupados = buffer_ocupados();

        log_evento("EMPACADOR", id, "ENTRA SC - toma producto",
                   p.nombre, ocupados);

        pthread_mutex_unlock(&mutex);

        log_evento("EMPACADOR", id, "SALE  SC",
                   p.nombre, ocupados);

        sem_signal_manual(&sem_empty);

        usleep((rand() % 1200 + 400) * 1000);
    }

    printf("[FIN] Empacador  #%d termino.\n", id);
    return NULL;
}

/* -------------------- HILO: TEMPORIZADOR -------------------- */
void *temporizador(void *arg)
{
    (void)arg;
    sleep(DURACION_SEG);
    simulacion_activa = 0;

    int i;
    for (i = 0; i < NUM_CAJEROS + NUM_EMPACADORES; i++) {
        sem_signal_manual(&sem_full);
        sem_signal_manual(&sem_empty);
    }
    return NULL;
}

/* -------------------- MAIN -------------------- */
int main(void)
{
    int i;
    pthread_t hilos_cajero[NUM_CAJEROS];
    pthread_t hilos_empacador[NUM_EMPACADORES];
    pthread_t hilo_timer;

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

    sem_inicializar(&sem_empty, BUFFER_SIZE);
    sem_inicializar(&sem_full,  0);
    pthread_mutex_init(&mutex, NULL);

    pthread_create(&hilo_timer, NULL, temporizador, NULL);

    for (i = 0; i < NUM_CAJEROS; i++) {
        int *id = malloc(sizeof(int));
        *id = i + 1;
        pthread_create(&hilos_cajero[i], NULL, cajero, id);
    }

    for (i = 0; i < NUM_EMPACADORES; i++) {
        int *id = malloc(sizeof(int));
        *id = i + 1;
        pthread_create(&hilos_empacador[i], NULL, empacador, id);
    }

    pthread_join(hilo_timer, NULL);
    for (i = 0; i < NUM_CAJEROS;     i++) pthread_join(hilos_cajero[i],    NULL);
    for (i = 0; i < NUM_EMPACADORES; i++) pthread_join(hilos_empacador[i], NULL);

    printf("--------------------------------------------------------------------------------\n");
    printf("                                FIN SIMULACIÓN \n");
    printf("--------------------------------------------------------------------------------\n");
    printf("  Productos Escaneados - Producidos: %d\n", total_producidos);
    printf("  Productos Empacados - consumidos: %d\n", total_consumidos);
    printf("  Productos en el Área de Empaque en el Fin: %d\n", total_producidos - total_consumidos);
    printf("--------------------------------------------------------------------------------\n");

    sem_destruir(&sem_empty);
    sem_destruir(&sem_full);
    pthread_mutex_destroy(&mutex);

    return 0;
}