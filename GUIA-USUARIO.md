# armiga — Guía de usuario

Bienvenido a armiga, tu consola de emulación Commodore Amiga.

## Controles

| Botón físico | Función |
|---|---|
| **B** | Confirmar / Seleccionar |
| **A** | Cancelar / Volver atrás |
| **X** | Eliminar (solo en pantalla de copias de seguridad) |
| **D-Pad** | Navegar por menús |
| **L1** | Cambiar idioma (Español/English) |
| **SELECT + START + L1** (mantener 3s) | Modo desarrollador |

## Menú principal

- **Catálogo Amiga** — accede a tu colección de juegos
- **Actualización de sistema** — comprueba e instala nuevas versiones de armiga
- **Diagnóstico del sistema** — información técnica del dispositivo (CPU, memoria, red, versiones)
- **Configuración** — todos los ajustes del dispositivo
- **Apagar dispositivo**

## Configuración

### Red inalámbrica
Introduce el nombre de tu red (SSID) y contraseña con el teclado en pantalla. Usa D-Pad para moverte entre teclas, B para seleccionar una letra, START para mayúsculas/minúsculas/números.

### Copia de seguridad
- **Crear**: genera una copia de tu configuración, partidas guardadas y ajustes de RetroArch. Se guardan hasta 3 copias; al crear una cuarta, se borra automáticamente la más antigua.
- **Restaurar**: elige una copia de la lista y pulsa B para restaurarla (el dispositivo se reiniciará). Pulsa X sobre una copia para eliminarla.

> Las copias de seguridad no incluyen ROMs ni kickstarts — solo tu configuración y progreso.

### LED RGB analógicos
Ajusta el color y brillo de los anillos LED de ambos sticks analógicos de forma independiente.

### Zona horaria
Selecciona tu zona horaria de una lista de ciudades. Afecta a la hora mostrada en el dispositivo.

### Ahorro de pantalla
Configura cuánto tiempo de inactividad debe pasar antes de que la pantalla se atenúe, y a qué porcentaje de brillo. Cualquier botón restaura el brillo al instante. Mientras la pantalla está atenuada, el dispositivo también reduce el consumo de batería.

### Brillo de pantalla
Ajusta el brillo general de la pantalla con D-Pad arriba/abajo, en pasos del 5%.

### SSH
Activa o desactiva el acceso remoto por SSH al dispositivo. Viene activado por defecto; si no lo vas a usar, puedes desactivarlo por seguridad.

### Restablecer valores de fábrica
Borra toda tu configuración personalizada (WiFi, ajustes, backups) y reinicia el dispositivo a su estado original. **Tus ROMs, kickstarts, partidas guardadas y estados no se ven afectados.**

## Actualizaciones de sistema

armiga comprueba si hay una versión más reciente disponible. Si la hay, puedes descargarla e instalarla directamente desde el menú — la descarga ocurre en segundo plano y el dispositivo se reiniciará automáticamente al terminar. No apagues el dispositivo durante una actualización.

## ¿Problemas?

- **El dispositivo no arranca tras una actualización**: armiga tiene un sistema de seguridad que revierte automáticamente a la versión anterior si detecta fallos repetidos de arranque, sin que tengas que hacer nada.
- **Olvidaste la contraseña WiFi guardada**: puedes volver a introducirla en Configuración → Red inalámbrica en cualquier momento.
- **Quieres empezar de cero**: usa Restablecer valores de fábrica desde Configuración.
