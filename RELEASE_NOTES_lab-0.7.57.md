# XCloud4 Lab 0.7.57-lab — Experimental Tester Release Notes

> **EXPERIMENTAL BUILD WARNING:** `XCloud4 Lab` is an experimental laboratory edition intended for advanced testers and protocol/decoder diagnostics. Some combinations of settings **may be unstable, freeze, or abort the stream**. For everyday play, use the **Public Beta (`XCloud4-1.2.0-beta-publico.pkg`)**.

---

## Package Information

- **File:** `XCloud4-0.7.57-lab.pkg`
- **PS4 Menu Title:** `XCloud4 Lab`
- **Title ID / Content ID:** `XCLD00001` (`IV0000-XCLD00001_00-XCLOUD4APP000000`)
- **Product Version / PS4 `APP_VER`:** `0.7.57-lab` / `01.25`
- **Size:** `17,498,112` bytes
- **SHA-256:** `48aa7120d12f29bd67596df91605d2335cf13d0ac29912a4778cb16d0048dc43`

### Side-by-Side Installation with the Public Beta

`XCloud4 Lab` uses its own Title ID (`XCLD00001`), menu title (`XCloud4 Lab`), and `LAB` badge icon (`assets/icon0-lab.png`). It can be installed on the PS4 **at the same time** as the Public Beta (`XCloud4`, `XCLD00003`) without overwriting it.

---

## What's New in 0.7.57-lab

- **Nueva interfaz, más rápida:**
  - Menú principal, catálogo (destacado panorámico y fila de carátulas), búsqueda, Cuenta, Fortnite, Ajustes (Imagen y Servidor), Acerca de y Apoyo rediseñados con paneles translúcidos, tipografía Inter e iconos Tabler.
  - La interfaz se dibuja en memoria normal y se copia a la pantalla solo escribiendo: en la PS4, moverse por `OPTIONS` pasó de ~600 ms a ~25 ms por redibujado.
  - Menú de partida nuevo que no afecta a la fluidez del juego.
  - Pantalla de carga con un cohete animado y barra de progreso.
  - Pantalla Apoyo con QR a `ko-fi.com/asapedgr` (las donaciones no dan acceso a juegos ni a servicios de Xbox).
  - Métricas en klog del dibujo de la interfaz (`XCloud4 UI draw: screen=… draw/copy/present p50/p95`) y de la fila (`fila Xbox inicio` / `fila Xbox fin espera_s=N resultado=…`).

- **Fila de Xbox cuando los servidores están llenos:**
  - Si Xbox te pone en fila (`WaitingForResources`), XCloud4 espera hasta 15 minutos y muestra `"Los servidores de Xbox están llenos. Estás en la fila… m:ss"` y, si Xbox la envía, una estimación orientativa del tiempo restante (`GET /v1/waittime/<titleId>`, campo `estimatedTotalWaitTimeInSeconds`). Antes, la app daba error a los 3 minutos.
  - Puedes cancelar en cualquier momento con `CÍRCULO`.

- **Nuevo botón Fortnite en el menú principal:**
  - Tarjeta **FORTNITE** en el menú principal para jugar gratis sin Game Pass (requiere cuenta de Microsoft con perfil de Xbox), con elección de calidad antes de jugar.
  - Pantalla previa de resumen con el servidor actual, sus ms reales de la última partida (o "sin medir") y elección de calidad (`1080p Alta`, `1080p` o `720p`).
  - Botón `OPTIONS` en el resumen para abrir directamente la pestaña de servidor y cambiar de región.
  - Al terminar la partida vuelve a la pantalla de resumen.

- **Selector de servidor con ms reales en OPTIONS (issue #4):**
  - Panel `OPTIONS` con pestañas **Imagen** y **Servidor** (navegables con `L1`/`R1`).
  - Muestreo por segundo del RTT de la sesión (`rtcGetRtt`) desde los 3 s de conectar; al cerrar una partida con 10 muestras o más, guarda la mediana (`p50`) en `/data/xcloud4/server-v1.bin` y la muestra junto a cada servidor.
  - Exportación en el JSON de sesión de `"rtt_ms": {"p50", "p95", "samples", "series"}` y metadatos del servidor seleccionado.

- **Petición de resolución y diagnóstico de recepción RTP/SRTP:**
  - Opción en el menú `R3` ("Petición de resolución"): `Cada 300 ms (anterior)`, `Una vez (como Edge)` y `Una vez y repetir cada 5 s sin vídeo`.
  - Contadores de red en el JSON de sesión: flujos RTP entrantes por SSRC/PT (`inbound_streams`), fallos de SRTP, SSRC/PT desconocidos y paquetes tras parada (`post_halt_video_packets`).

- **Espera de 30 s en el arranque de vídeo y vigilancia de la cola de entrada:**
  - Espera de hasta 30 s mostrando el progreso mientras Xbox prepara el envío de vídeo.
  - Recuperación automática ante bloqueos de la cola de entrada (>500 ms llena sin avance del consumidor) con reinicio de ensamblado y solicitud de keyframe (`ingress_stall_recoveries`).
  - Mensaje claro al agotar el tiempo sin vídeo, recomendando volver a intentarlo o elegir otro servidor en `OPTIONS`.

- **Ajustes de conversión de color y nitidez en R3:**
  - Conversión `Original` / `Suave` (croma bilineal 4:2:0 -> 4:4:4 + escalado uniforme 720p a 1080p) y Nitidez `No` / `Baja` / `Media`, con telemetría de tiempos en `conversion_tuning`.

- **Certificados raíz propios para firmwares antiguos (issue #3):**
  - Incluye raíces TLS de DigiCert, Microsoft, USERTrust y Sectigo (de la lista de Mozilla) para consolas con lista del sistema desactualizada (`0x8095F00C`). Probado en firmware 12.00 (sin confirmar en 5.05).

- **Mensajes claros de cuenta, red y disponibilidad:**
  - Mensajes traducidos para códigos XSTS (`2148916233`, `2148916235`, `2148916236`/`2148916237`, `2148916238`), pistas de red (DNS, timeout, certificados) y mensaje explícito cuando Xbox rechaza las dos ofertas (`xgpuweb` y `xgpuwebf2p`) con `4xx` y `xerr == 0`: `"Xbox Cloud Gaming no está disponible para tu ubicación o tu cuenta (HTTP <código>). XCloud4 solo puede usar los servidores que Xbox te ofrece."` / `"Xbox Cloud Gaming isn't available for your location or account (HTTP <code>). XCloud4 can only use the servers Xbox offers you."` Esa pantalla muestra además un QR a las instrucciones de [docs/region-es.md](https://github.com/asp11edgr/XCloud4/blob/main/docs/region-es.md) ([docs/region-en.md](https://github.com/asp11edgr/XCloud4/blob/main/docs/region-en.md) en inglés).

---

## Requirements

- **Console:** PlayStation 4 running **GoldHEN**. Probado en: PS4 Fat, firmware 12.00, GoldHEN v2.4b18.7. Si la usas en otro modelo o firmware, cuéntanos en un issue si funciona.
- **Subscription / Account:** An Xbox Game Pass plan that includes cloud gaming, or a free Microsoft account with an Xbox profile for Fortnite.
- **Network:** A stable internet connection of at least **15 Mbps** (wired Ethernet recommended).

---

## Aviso / Disclaimer

Aviso: XCloud4 es un proyecto independiente, sin afiliación con Microsoft, Xbox ni Sony. Se ofrece tal cual, sin garantías. Lo usas bajo tu propia responsabilidad: el proyecto no se hace responsable de problemas con tu consola, tu cuenta o tus suscripciones. Usa XCloud4 respetando los términos de servicio de Microsoft y Xbox.

Disclaimer: XCloud4 is an independent project, not affiliated with Microsoft, Xbox or Sony. It is provided as is, without warranty. You use it at your own risk: the project is not responsible for problems with your console, account or subscriptions. Use XCloud4 in accordance with Microsoft and Xbox terms of service.
