# Explicación de funcionamiento de los módulos

## `toggle_active_cpu.ko`

Diseñado para gestionar dinámicamente la asignación y suspensión de CPUs según las necesidades de los procesos del sistema.

### Características Principales

1. **Gestión de Monopolización de CPUs**:
   - Monitorea qué procesos solicitan monopolizar un CPU.
   - Libera CPUs monopolizados si el proceso que los estaba utilizando ya no tiene alta prioridad.
   - Monopoliza CPUs si hay procesos de alta prioridad solicitándolo.

2. **Recolección de Estadísticas**:
   - Recopila estadísticas sobre la carga del sistema y las necesidades de los procesos.
   - Calcula un puntaje (`stats_score`) basado en la carga de usuario y las necesidades de procesos.

3. **Encendido y Apagado Dinámico de CPUs**:
   - Apaga CPUs cuando el sistema está en baja carga por un tiempo prolongado.
   - Enciende CPUs suspendidos si la carga del sistema aumenta o si algún proceso lo requiere.

4. **Timers Periódicos**:
   - Utiliza timers para ejecutar tareas recurrentes:
     - **`timer_callback_monopolization`**: Gestiona la monopolización de CPUs.
     - **`timer_callback_stats`**: Recolecta estadísticas del sistema.
     - **`timer_callback_turn_off`**: Decide si puede suspender un CPU.
     - **`timer_callback_turn_on`**: Reactiva CPUs suspendidos si es necesario.

## Funcionalidades Clave

### 1. **`event_handler`**
   - Gestiona la carga (`MOD_LOAD`) y descarga (`MOD_UNLOAD`) del módulo:
     - Configura los timers.
     - Inicializa el estado de los CPUs.
     - Libera recursos al descargar el módulo.

### 2. **Timers y Callbacks**
   - **`timer_callback_monopolization`**: Gestiona la monopolización de CPUs.
   - **`timer_callback_stats`**: Calcula un puntaje basado en la carga del sistema y las necesidades de los procesos.
   - **`timer_callback_turn_off`**: Suspende CPUs tras mediciones consecutivas de baja carga.
   - **`timer_callback_turn_on`**: Reactiva CPUs suspendidos si la carga del sistema aumenta.

### 3. **Funciones de Ayuda**
   - **`get_idlest_cpu`**: Identifica el CPU menos cargado.
   - **`get_monopolization_info`**: Obtiene información de procesos que solicitan monopolizar CPUs.
   - **`get_user_load`** y **`get_procs_needs`**: Calculan cargas y necesidades de procesos para determinar `stats_score`.

### 4. **Definiciones de Carga**
   - **`calc_load_score`**: Normaliza puntajes de carga en categorías: *IDLE*, *LOW*, *NORMAL*, *HIGH*, *INTENSE*, *SEVERE*.

## Variables Importantes

- **Timers**: `timer_monopolization`, `timer_stats`, `timer_turn_off`, `timer_turn_on`.
- **`turned_off_cpus`**: Arreglo booleano que indica cuáles CPUs están suspendidos.
- **`stats_score`**: Puntaje basado en la carga del sistema.
- **`MAX_TURNED_OFF`**: Límite de CPUs que pueden estar suspendidos simultáneamente.

# Módulo `cpu_stats` para FreeBSD

Este módulo permite monitorear el uso de la CPU en cada núcleo y expone estos datos a través de `sysctl`.

## Características principales

### 1. Exposición de Estadísticas de CPU mediante `sysctl`
- `sysctl_get_cpu_stats()` expone `cpu_stats` a través de `sysctl`.
- `SYSCTL_PROC()` registra `cpu_stats` bajo `_kern`.

### 2. Registro del Uso de CPU
- Se rastrea el tiempo de ejecución en diferentes estados: `USER`, `NICE`, `SYS`, `INTR`, `IDLE`.
- `calc_cpu_stats()` calcula diferencias (delta) en el uso de la CPU.

### 3. Registro de Estadísticas en el Log
- `log_cpu_stats()` escribe en el log del sistema.

### 4. Muestreo Periódico mediante un Timer
- `timer_callback()` actualiza periódicamente las estadísticas.

### 5. Manejo del Ciclo de Vida del Módulo
- `event_handler()` gestiona `MOD_LOAD` y `MOD_UNLOAD`.

## Uso y Aplicaciones
- Monitoreo del rendimiento del sistema.
- Evaluación de algoritmos de planificación de CPU en el kernel.

# Módulo `pin_threads` para FreeBSD

Permite fijar hilos a CPUs específicos para garantizar su ejecución en los mismos núcleos.

### Características principales

1. **Fijación de Hilos a CPUs**
   - `monopolize_cpu(0, 2)`: Fija los hilos a la CPU 0 y el núcleo 2.
   - `release_cpu(0, 2)`: Libera la fijación en `MOD_UNLOAD`.

2. **Temporización con Callout**
   - `timer_callback()` reactiva el temporizador con una frecuencia aleatoria.

3. **Manejo del Ciclo de Vida del Módulo**
   - `event_handler()` maneja `MOD_LOAD` y `MOD_UNLOAD`.

## Uso y Aplicaciones
- Mejor rendimiento en sistemas de tiempo real o cargas de trabajo específicas.

# Módulo `thread_stats` para FreeBSD

Este módulo recopila y expone estadísticas sobre hilos y procesos en el sistema.

## Características principales

### 1. Exposición de Estadísticas mediante `sysctl`
- `sysctl_get_threads_stats()`: Expone `threads_stats`.
- `sysctl_get_monopolization_info()`: Expone `cpu_monopolization_info`.

### 2. Recolección de Estadísticas de Hilos y Procesos
- `read_thread_stats()` obtiene estadísticas de cada proceso.

### 3. Identificación de Procesos que Monopolizan CPUs
- `get_proc_metadata_sched_info()` detecta monopolización de CPUs.

### 4. Registro de Estadísticas en el Log
- `log_threads_stats()` escribe en el log del sistema.

### 5. Temporización con Callout
- `timer_callback()` actualiza las estadísticas periódicamente.

### 6. Manejo del Ciclo de Vida del Módulo
- `event_handler()` gestiona `MOD_LOAD` y `MOD_UNLOAD`.

## Uso y Aplicaciones
- Monitoreo del uso de hilos y procesos.
- Evaluación del planificador en FreeBSD.