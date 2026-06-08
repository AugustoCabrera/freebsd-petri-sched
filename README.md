<img width="7149" height="1968" alt="1_1_Isologotipo FCEFyN- original_Sin fondo-Con bajada" src="https://github.com/user-attachments/assets/22a1144b-c3a3-4b62-8787-68c64a659f09" />


# Proyecto Integrador: Proyecto Scheduler

<p align="center">
  <img src="https://img.shields.io/badge/Language-C-00599C?style=flat-square&logo=c" alt="C" />
  <img src="https://img.shields.io/badge/Language-Assembly-6E4C13?style=flat-square&logo=assembly" alt="Assembly" />
  <img src="https://img.shields.io/badge/OS-FreeBSD_14.1-AB2B28?style=flat-square&logo=freebsd" alt="FreeBSD" />
  <img src="https://img.shields.io/badge/Model-Redes_de_Petri-6A0DAD?style=flat-square" alt="Redes de Petri" />
</p>

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

##  Informe Final

El documento completo con el marco teórico, el desarrollo de la implementación y las validaciones de este Proyecto Integrador se encuentra disponible para su lectura y descarga:

**[Descargar Informe del Proyecto Integrador (PDF)](https://github.com/AugustoCabrera/freebsd-petri-sched/releases/download/v1.0.0-rc.1/proyecto_integrador_Augusto_Cabrera.pdf)**


## Resumen de Cambios e Implementación

Actualmente, la rama `main` contiene el proyecto integrador finalizado. La rama principal partió del código base correspondiente al proyecto integrador anterior del Proyecto Scheduler.

**[Ver Pull Request #17 (Detalle exacto de contribuciones y código modificado)](https://github.com/AugustoCabrera/freebsd-petri-sched/pull/17)**

> Revisando la pestaña de *Files changed* en dicho Pull Request, se puede observar el **diff exacto** y aislado de todas las líneas de código agregadas y modificadas durante este ciclo de desarrollo, diferenciando claramente las nuevas implementaciones del proyecto integrador base. 
> 
> **Línea de investigación y trabajo base:** Este trabajo se construye sobre y extiende la arquitectura desarrollada en la tesis *"Scheduling guiado por tipo de proceso para FreeBSD"* (Autores originales: Francisco Ignacio Bonino y Francisco Daniele), la cual forma parte de una serie de iteraciones previas dentro de esta línea de investigación.
