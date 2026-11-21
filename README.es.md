# FF8 Reborn

**[Descargar el instalador para Windows](https://github.com/Faga250885/FF8-Reborn/releases/latest/download/FF8-Reborn-Setup.exe)** · [Read in English](README.md) · [Galería de capturas](docs/GALERIA.md)

FF8 Reborn es un mod visual gratuito para **FINAL FANTASY VIII Remastered** de Steam. Permite elegir los FPS originales o apuntar a 60 o 120 FPS, ampliar la cámara de batallas y mapamundi y ajustar cómo se integran los personajes en los fondos prerenderizados. Los cambios se pueden probar mientras juegas. El menú y el instalador están en inglés.

[![Carrusel de filtros, batallas panorámicas, mapa, difuminado y ajustes](docs/images/tour.gif)](docs/GALERIA.md)

[Recorrer las 16 capturas a tu ritmo →](docs/GALERIA.md)

## Requisitos

- Un PC con Windows y la cuenta de Steam propietaria de [FINAL FANTASY VIII Remastered (app 1026680)](https://store.steampowered.com/app/1026680/FINAL_FANTASY_VIII__REMASTERED/).
- La edición compatible de Steam para Windows con `FFVIII_EFIGS.dll`. El instalador comprueba el motor y la DLL de audio original. La edición de PC de 2013 y la de PlayStation usan otros motores.
- Inicia el juego desde Steam. Al arrancar, el mod comprueba la aplicación, la licencia y la cuenta propietaria. Una copia prestada o de fin de semana gratuito no activa el mod. La descarga contiene únicamente el mod.
- Para ver 120 FPS, usa una pantalla de al menos 120 Hz y selecciona esa frecuencia en Windows. Los cuadros que llegan a pantalla dependen de la escena, la GPU y el monitor.

## Instalar y jugar

1. Instala el juego en Steam y cierra tanto FFVIII como su lanzador.
2. Descarga y ejecuta **[FF8-Reborn-Setup.exe](https://github.com/Faga250885/FF8-Reborn/releases/latest/download/FF8-Reborn-Setup.exe)**. Este instalador contiene el mod completo. Puedes usarlo sin un gestor de mods ni Lossless Scaling.
3. Revisa **Game folder**. Si hace falta, usa **Browse** y elige la carpeta que abre Steam desde **Administrar → Ver archivos locales**.
4. Elige los ajustes y pulsa **Install with these settings**. El instalador conserva una copia de la DLL de audio original.
5. Abre el juego desde Steam. Pulsa **F8** o **Select + Start** para abrir el menú del mod.

![Instalador con opciones y vista previa del filtro](docs/images/installer.png)

## Opciones

| Opción del menú | Para qué sirve |
| --- | --- |
| Frame rate | Elige **Original**, **60** o **120**. El mod dibuja cuadros intermedios para los movimientos compatibles. Se mantienen los tiempos de animación y de entrada en combate. Algunos cuadros pueden mostrarse a su cadencia original. |
| Aspect ratio | **Widescreen** amplía la cámara de batallas y mapamundi. **Original (4:3)** recupera el encuadre original. |
| Blurred sidebars | En pantalla ancha, rellena los lados de los fondos fijos con colores difuminados de la escena, incluidos los personajes que se acercan a los bordes. |
| Full-screen map | Alterna entre el mapa **Original** y el atlas ilustrado **Reborn**. El mapa completo queda centrado y conserva sus proporciones. |
| Screen filter | Permite elegir un perfil y ajustar resolución, pixelado, líneas de barrido, máscara RGB, resplandor, tramado PS1 y suavizado de bordes. Los controles disponibles dependen del tipo de filtro. |

**FF8 Reborn (recommended)** combina 320 líneas con líneas de barrido y tramado suaves. **HD Blend** conserva una apariencia de mayor resolución. Compara colores cercanos a dos distancias, suaviza los cambios bruscos de color y limita la variación de brillo en los detalles finos. Puedes graduarlo con **Edge smoothing**. [Comparar HD Blend, el fotograma original y el filtro recomendado](docs/GALERIA.md#hd-blend).

También puedes elegir **PS1 Digital**, **Soft CRT**, **PS1 · RGB Monitor**, **PS1 Backgrounds**, **PS1 · CRT TV**, **PS1 Sharp** u **Off**.

## Menú y atajos

| Acción | Teclado | Mando XInput / Steam Input |
| --- | --- | --- |
| Abrir o cerrar el panel | F8 | Select + Start (Vista + Menú en Xbox) |
| Recorrer las opciones | Arriba / Abajo | Cruceta o palanca izquierda arriba / abajo |
| Cambiar un valor o perfil | Izquierda / Derecha | Cruceta o palanca izquierda a los lados |
| Ajustar un número en pasos mayores | Mayús + Izquierda / Derecha | Mantener el botón superior derecho (RB) al ajustar |
| Ejecutar la acción seleccionada | Intro | A |
| Descartar la edición del filtro y cerrar | Esc | B |

Las opciones de pantalla se guardan al cambiarlas. Los filtros tienen sus propios perfiles. Para crear el tuyo, elige un perfil, modifícalo y pulsa **Create a custom copy**. Si ya tienes una copia, **Save to my custom profile** sustituye ese único perfil personal. Al editarlo, usa **Save my changes**. **Restore filter defaults** aparece cuando los valores difieren de los originales del filtro base. El juego recuerda el último perfil elegido.

![Menú del mod dentro del juego](docs/images/settings.png)

## Actualizar, desinstalar y resolver problemas

- **Actualizar.** Cierra el juego y su lanzador y abre el último instalador. Lee las preferencias existentes al abrirse. Revisa la selección antes de aplicarla.
- **Desinstalar.** Cierra FFVIII y el lanzador, selecciona la carpeta del juego en el instalador y pulsa **Uninstall**. Se verifica y restaura la DLL original. La instalación no modifica las partidas.
- **F8 no responde.** Dale el foco a la ventana del juego y ábrelo desde la cuenta de Steam propietaria. En el mando pulsa ambos botones a la vez. Si tu mando no ofrece XInput, usa una distribución de mando en Steam Input.
- **DLL inesperada o motor incompatible.** Comprueba la edición y la carpeta. Si otro mod utiliza `libvgmstream.dll`, retíralo con sus propias instrucciones antes de instalar FF8 Reborn.
- **Informar de un fallo visual.** Indica lugar, tipo de escena, FPS elegidos, formato de pantalla y filtro. Añade una captura si es un defecto fijo o un vídeo corto si ocurre en movimiento. [Abrir una incidencia](https://github.com/Faga250885/FF8-Reborn/issues).

## Proyecto

Creado por **Faga12**. FF8 Reborn es un proyecto de aficionados independiente. FINAL FANTASY VIII Remastered pertenece a sus respectivos titulares. [Compilación del proyecto](docs/BUILD.md) · [Versiones y sumas SHA-256](https://github.com/Faga250885/FF8-Reborn/releases).
