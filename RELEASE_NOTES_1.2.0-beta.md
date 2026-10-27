# XCloud4 1.2.0-beta — Public Beta Release Notes

**XCloud4** is an independent, native Xbox Cloud Gaming client for PlayStation 4 consoles running GoldHEN, built with the open-source OpenOrbis PS4 Toolchain.

> **Mission:** Helping more people play next-gen games without buying a new console.

---

## What's New in 1.2.0-beta

- **Nueva interfaz, más rápida:**
  - Menú principal, catálogo (destacado panorámico y fila de carátulas), búsqueda, Cuenta, Fortnite, Ajustes (Imagen y Servidor), Acerca de y Apoyo rediseñados con paneles translúcidos, tipografía Inter e iconos Tabler.
  - La interfaz se dibuja en memoria normal y se copia a la pantalla solo escribiendo: en la PS4, moverse por `OPTIONS` pasó de ~600 ms a ~25 ms por redibujado.
  - Menú de partida nuevo que no afecta a la fluidez del juego.
  - Pantalla de carga con un cohete animado y barra de progreso.
  - Pantalla Apoyo con QR a `ko-fi.com/asapedgr` (las donaciones no dan acceso a juegos ni a servicios de Xbox).

- **Fila de Xbox cuando los servidores están llenos:**
  - Si Xbox te pone en fila (`WaitingForResources`), XCloud4 espera hasta 15 minutos y muestra `"Los servidores de Xbox están llenos. Estás en la fila… m:ss"` y, si Xbox la envía, una estimación orientativa del tiempo restante. Antes, la app daba error a los 3 minutos.
  - Puedes cancelar en cualquier momento con `CÍRCULO`.

- **Nuevo botón Fortnite en el menú principal:**
  - Tarjeta **FORTNITE** en el menú principal para jugar gratis sin Game Pass (requiere cuenta gratuita de Microsoft con perfil de Xbox), con elección de calidad antes de jugar.
  - Pantalla previa de resumen con el servidor actual, sus ms reales de la última partida (o "sin medir") y elección de calidad (`1080p Alta`, `1080p` o `720p`).
  - Botón `OPTIONS` en el resumen para abrir directamente la pestaña de servidor y cambiar de región.
  - Al terminar la partida vuelve a la pantalla de resumen.

- **Selector de servidor con ms reales en OPTIONS (issue #4):**
  - Nueva pestaña **Servidor** en el menú `OPTIONS` (con `L1`/`R1` para alternar entre **Imagen** y **Servidor**).
  - Muestra los servidores que Xbox ofrece a tu cuenta (`offeringSettings.regions`). Tras jugar una partida en un servidor (10 muestras o más), guarda y muestra los ms reales (mediana del RTT de la sesión) de la última partida en ese servidor.
  - Por defecto usa **Automático (recomendado)** (el servidor que Xbox asigna como predeterminado). Puedes elegir manualmente cualquier otro servidor de la lista.
  - Al iniciar sesión por primera vez, se abre el selector para elegir servidor antes de cargar el catálogo.
  - La elección y los ms por servidor se guardan en `/data/xcloud4/server-v1.bin`. Al cambiar de servidor desde el catálogo o el resumen de Fortnite, la consulta se recarga automáticamente en el nuevo servidor.

- **Menú de calidad de imagen en OPTIONS ("Ajustes de imagen"):**
  - Pulsa `OPTIONS` en el menú principal, en Cuenta o en el catálogo para elegir la calidad del stream:
    - **1080p Alta (recomendada):** 1920x1080 a 60 fps (~10–13 Mbps, perfil G). La mejor nitidez para conexiones estables de 15 Mbps o más (mejor por cable).
    - **1080p:** 1920x1080 a 60 fps (~5–8 Mbps, perfil G8). Consume menos internet; recomendable si `1080p Alta` da tirones.
    - **720p:** 1280x720 a 60 fps (~7 Mbps, perfil 720HQ). Menor carga de decodificación.
  - La preferencia se guarda en `/data/xcloud4/settings-public-v1.bin` y se anuncia junto con el servidor activo en el cartel de 6 segundos al iniciar el vídeo.
  - VSync desactivado para menor latencia.

- **Imagen más nítida (color suave, escalado bilineal de 720p y realce ligero):**
  - Reconstrucción de croma bilineal (H.264 4:2:0 a 4:4:4) que suaviza bordes de color y texto.
  - Escalado bilineal uniforme de 720p a 1080p y realce espacial ligero de nitidez acelerado con SSE2.

- **Espera de hasta 30 s del primer vídeo con aviso de progreso:**
  - Si Xbox tarda en enviar el primer fotograma de vídeo, la aplicación espera hasta 30 segundos mostrando el progreso en pantalla antes de dar por fallido el arranque.
  - Si transcurren los 30 s sin vídeo, informa claramente: `"Xbox no envió imagen. Puede ser un fallo temporal de Xbox con este juego: vuelve a intentarlo o prueba otro servidor en OPTIONS."` / `"Xbox didn't send any video. It may be a temporary Xbox problem with this game: try again or pick another server in OPTIONS."`

- **Certificados raíz propios para firmwares antiguos (issue #3):**
  - Incluye su propia copia de los certificados raíz usados por Microsoft, Xbox y GitHub (DigiCert, Microsoft, USERTrust y Sectigo, de la lista de Mozilla) para consolas en firmwares antiguos (como 5.05) cuya lista del sistema está desactualizada (`0x8095F00C`).
  - Probado en firmware 12.00 (sin confirmar aún en 5.05). Si el error de certificado persiste, la pantalla muestra una pista explicativa.

- **Mensajes claros de cuenta, red y disponibilidad:**
  - Mensajes traducidos para códigos XSTS de Xbox: cuenta sin perfil de Xbox (`2148916233`), país no soportado (`2148916235`), verificación de edad (`2148916236`/`2148916237`) y permiso familiar para cuenta de menor (`2148916238`).
  - Si Xbox rechaza el acceso a la nube en las dos ofertas (`xgpuweb` y `xgpuwebf2p`) con `4xx` y sin `XErr`, muestra: `"Xbox Cloud Gaming no está disponible para tu ubicación o tu cuenta (HTTP <código>). XCloud4 solo puede usar los servidores que Xbox te ofrece."` / `"Xbox Cloud Gaming isn't available for your location or account (HTTP <code>). XCloud4 can only use the servers Xbox offers you."`
  - Esa pantalla muestra además un QR a las instrucciones de [docs/region-es.md](https://github.com/asp11edgr/XCloud4/blob/main/docs/region-es.md) ([docs/region-en.md](https://github.com/asp11edgr/XCloud4/blob/main/docs/region-en.md) en inglés).
  - Pistas de diagnóstico de red para errores de DNS (`1.1.1.1` / `8.8.8.8`), tiempo de espera agotado y certificados TLS.

- **Mejoras anteriores conservadas:**
  - Código QR directo en el inicio de sesión (`microsoft.com/link?otc=<código>`).
  - Aviso de nuevas versiones consultando GitHub Releases al arrancar.
  - Motor de audio adaptativo Opus 48 kHz con bloques de 512 muestras.
  - Menú del juego manteniendo dos dedos en el touchpad durante 1 segundo.

---

## Package Information

- **File:** `XCloud4-1.2.0-beta-publico.pkg`
- **PS4 Menu Title:** `XCloud4`
- **Title ID / Content ID:** `XCLD00003` (`IV0000-XCLD00003_00-XCLOUD4PUBLIC000`)
- **Product Version / PS4 `APP_VER`:** `1.2.0-beta` / `01.06`
- **Size:** `16,646,144` bytes
- **SHA-256:** `457473f98c9acc16c7f217781331271985f3a716172bdf2f439373222f7b4209`

---

## Requirements

- **Console:** PlayStation 4 running **GoldHEN**. Probado en: PS4 Fat, firmware 12.00, GoldHEN v2.4b18.7. Si la usas en otro modelo o firmware, cuéntanos en un issue si funciona.
- **Subscription / Account:** An Xbox Game Pass plan that includes cloud gaming (PC Game Pass does not), or a free Microsoft account with an Xbox profile for Fortnite.
- **Network:** A stable internet connection of at least **15 Mbps** (wired Ethernet recommended).

---

## Installation

1. Copy `XCloud4-1.2.0-beta-publico.pkg` to your PS4 using a USB drive or via the GoldHEN FTP server to `/data/pkg/`.
2. On the PS4, open **GoldHEN > Debug Settings > Package Installer** and install the package.
3. Launch **XCloud4** from the PS4 home screen and sign in with your Microsoft account using the on-screen QR code or code at `microsoft.com/link`.

---

## Controls

- **OPTIONS (fuera del juego / outside of game):** Abre Ajustes (**Imagen** y **Servidor**, alternando con `L1`/`R1`).
- **Dos dedos 1 s en el touchpad (en el juego / in game):** Abre el menú del juego (`Volver al catálogo`, `Silenciar/Activar sonido`, `Botón Xbox`, `Cancelar`).
- **Salir / Exit:** **Botón PS > Cerrar aplicación** (`PS button > Close Application`).

---

## Aviso / Disclaimer

Aviso: XCloud4 es un proyecto independiente, sin afiliación con Microsoft, Xbox ni Sony. Se ofrece tal cual, sin garantías. Lo usas bajo tu propia responsabilidad: el proyecto no se hace responsable de problemas con tu consola, tu cuenta o tus suscripciones. Usa XCloud4 respetando los términos de servicio de Microsoft y Xbox.

Disclaimer: XCloud4 is an independent project, not affiliated with Microsoft, Xbox or Sony. It is provided as is, without warranty. You use it at your own risk: the project is not responsible for problems with your console, account or subscriptions. Use XCloud4 in accordance with Microsoft and Xbox terms of service.
