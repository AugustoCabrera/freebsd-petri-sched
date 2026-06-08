<img width="1728" height="385" alt="image" src="https://github.com/user-attachments/assets/cdc1ac66-de21-4214-b373-2e03e90a45e4" />


# Proyecto Integrador: Proyecto Scheduler

Este repositorio contiene el Proyecto Integrador desarrollado para obtener el título de **Ingeniero en Computación** en la **FCEFyN** (Facultad de Ciencias Exactas, Físicas y Naturales) de la **Universidad Nacional de Córdoba**. 

El trabajo se enfoca en la integración final de la extensión y validación de mecanismos de control del despacho de CPU en el scheduler de corto plazo de FreeBSD, utilizando **Redes de Petri** como modelo formal. 

El proyecto toma como base las implementaciones de iteraciones anteriores del "Proyecto Scheduler" y las actualiza a la rama `releng/14.1` (correspondiente a [FreeBSD 14.1-RELEASE](https://github.com/freebsd/freebsd-src/tree/releng/14.1)), sumando nuevas características de control, reserva de núcleos e instrumentación.

## Objetivo General

Aportar al Proyecto Scheduler mediante el diseño, la extensión y la validación de mecanismos de control del despacho de CPU en el scheduler de corto plazo de FreeBSD, utilizando Redes de Petri como modelo formal para describir y gobernar el comportamiento del sistema.

## Objetivos Específicos Alcanzados

* **Análisis de base:** Análisis en profundidad del funcionamiento del scheduler 4BSD de FreeBSD y los modelos basados en Redes de Petri desarrollados en trabajos previos del Proyecto Scheduler.
* **Modelo a nivel de proceso:** Diseño de una Red de Petri a nivel de proceso que modela el estado interno de ejecución, estableciendo criterios formales para determinar qué hilos se encuentran en condiciones de ejecutarse, sirviendo como marco conceptual para futuras extensiones.
* **Máquina de Estados Finitos (FSM):** Reformulación del modelo existente de hilos transformando su Red de Petri en una FSM, preservando su estructura formal y estableciendo una relación jerárquica clara con la Red de Petri de recursos.
* **Control estricto de despacho:** Implementación y validación de mecanismos de control del despacho de CPU gobernados exclusivamente por la Red de Petri de recursos, verificando la ausencia de caminos alternativos que eludan dicho control.
* **Gestión de núcleos (Core Reservation):** Diseño e implementación de un esquema de suspensión y habilitación selectiva de núcleos de CPU que permite la noción de cores reservados o de uso preferencial para determinados procesos o hilos.
* **Instrumentación estructurada:** Incorporación de un mecanismo de instrumentación liviana en el kernel que registra las transiciones de la Red de Petri de recursos en formato estructurado, facilitando el análisis y la validación del comportamiento del scheduler.

## Estructura de Desarrollo

El desarrollo de los objetivos específicos se llevó a cabo mediante iteraciones modulares. Cada iteración fue trabajada en su respectiva rama (*branch*) antes de ser integrada a la rama `main`:

* `feature/benchmark-resource-net`
* `feature/core-disabled`
* `feature/core-reserved`
* `feature/fsm-thread-net`

## Resumen de Cambios e Implementación

Actualmente, la rama `main` contiene el proyecto integrador finalizado. Dado que la rama principal partió del código base correspondiente al proyecto integrador anterior del Proyecto Scheduler.

**[Ver Pull Request #17 (Detalle exacto de contribuciones y código modificado)](https://github.com/AugustoCabrera/freebsd-petri-sched/pull/17)**

> Revisando la pestaña de *Files changed* en dicho Pull Request, se puede observar el **diff exacto** y aislado de todas las líneas de código agregadas y modificadas durante este ciclo de desarrollo, diferenciando claramente las nuevas implementaciones del proyecto integrador base. 
> 
> **Línea de investigación y trabajo base:** Este trabajo se construye sobre y extiende la arquitectura desarrollada en la tesis *"Scheduling guiado por tipo de proceso para FreeBSD"* (Autores originales: Francisco Ignacio Bonino y Francisco Daniele), la cual forma parte de una serie de iteraciones previas dentro de esta línea de investigación.
