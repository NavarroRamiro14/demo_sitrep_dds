# Demo SITREP - OpenDDS (RTPS P2P)

Este proyecto es una Prueba de Concepto (PoC) para el sistema **AR-TDC**. Implementa un mecanismo de publicación y suscripción de mensajes SITREP (Situational Report) utilizando **OpenDDS** en C++.

La arquitectura es **Peer-to-Peer (P2P)** utilizando el protocolo **RTPS** (Real-Time Publish-Subscribe), lo que permite que los nodos se descubran y comuniquen automáticamente sin necesidad de un servidor central (`DCPSInfoRepo`). Además, incluye un sistema de **Sincronización Inicial (Snapshot)** para nodos que se unen tarde a la red (Late-Joiners) y persistencia local en SQLite.

## 📋 Requisitos Previos

Para compilar y ejecutar este proyecto, necesitas tener instalado y configurado el entorno de **OpenDDS** (incluyendo ACE+TAO).

Asegúrate de tener cargadas las variables de entorno antes de empezar. Usualmente:

```bash
source ~/OpenDDS/setenv.sh

```
_(También se requiere la librería de SQLite3. Si no la tienes, instálala con `sudo apt install libsqlite3-dev`)_

## 🚀 Compilación

Sigue estos pasos para generar los binarios desde cero:

1. **Clonar el repositorio:**
```bash
git clone https://github.com/NavarroRamiro14/demo_sitrep_dds.git
cd demo_sitrep_dds

```


2. **Generar el Makefile:**
Utilizamos MPC (Make Project Creator) para generar el Makefile compatible con tu sistema.
```bash
$ACE_ROOT/bin/mwc.pl -type gnuace

```


3. **Compilar:**
```bash
make

```


Esto generará un ejecutable llamado `operador`.

## ⚙️ Ejecución

Para probar la comunicación, necesitas simular al menos dos nodos (pueden ser dos terminales en la misma PC o dos PCs diferentes en la misma red LAN).

**Es obligatorio iniciar el programa pasando un parámetro numérico de prioridad.** Este número define qué nodo tiene autoridad (Exclusive Ownership) para responder a las solicitudes de sincronización de los nuevos nodos. **A mayor número, mayor prioridad.**

### Paso 1: Configuración de Red

El proyecto utiliza el archivo `rtps.ini` para la configuración de descubrimiento. Asegúrate de que este archivo esté en la misma carpeta que el ejecutable.

### Paso 2: Iniciar Nodos

**Terminal 1 (Ej: Nodo LIDER):**

```bash
./operador 100 -DCPSConfigFile rtps.ini

```

* Ingresa un ID único cuando se te solicite (ej: `ALFA`).

**Terminal 2 (Ej: Nodo NUEVO):**

```bash
./operador 10 -DCPSConfigFile rtps.ini

```

* Ingresa un ID diferente (ej: `BETA`).

## 🎮 Uso

El programa funciona simultáneamente como **Publicador** y **Suscriptor** y **Gestor de BD Local**.

* **`p` (Publicar)** : Presiona esta tecla y luego `Enter` para iniciar la carga de un nuevo SITREP. Sigue las instrucciones en pantalla para ingresar Track ID, Identidad, Latitud, Longitud e Info.
* **`q` (Salir)**: Salir del programa.
* **`l` (Listar)**: Muestra por pantalla todos los registros actuales guardados en la base de datos local SQLite (artdc_tactical.db). Ideal para ver todas las trazas conocidas sin necesidad de salir.
* **Recepción**: Los mensajes de otros nodos aparecerán automáticamente en pantalla.

> **Nota sobre la Interfaz:** Al ser una demo de consola, si recibes un mensaje mientras estás escribiendo otro, el texto en pantalla podría "mezclarse" visualmente. Esto es una limitación de la terminal y no afecta la integridad de los datos ni sucederá en la interfaz gráfica final (Qt).

## 📂 Estructura del Proyecto

* **`Sitrep.idl`**: Definición de la estructura de datos (IDL). Aquí se define la `@key trackId` para la unicidad de las trazas.
* **`Operador.cpp`**: Código fuente principal. Contiene la lógica del `main`, el `DataWriter` y el `DataReaderListener`.
* **`SitrepDatabase.h`**: Lógica de base de datos local utilizando SQLite3. Crea y actualiza automáticamente el archivo `artdc_tactical.db`.
* **`rtps.ini`**: Configuración de transporte OpenDDS (UDP/Multicast).
* **`demo_sitrep.mpc`**: Archivo de definición del proyecto para el sistema de compilación.

