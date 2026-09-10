# Solución para el Trabajo de Compilación Fallido

## Problema

El proceso de compilación está fallando debido a un **problema de compatibilidad con coreutils** en el comando `install`. El mensaje de error indica:

```
Please change to coreutils install with:
update-alternatives --install /usr/bin/install install /usr/bin/gnuinstall 100
```

Este es un problema conocido (https://github.com/uutils/coreutils/issues/12166) donde el sistema de compilación espera la versión GNU del comando `install`, pero el sistema tiene una versión conflictiva instalada.

## Solución

Añade el siguiente paso al flujo de trabajo en `.github/workflows/build.yml` inmediatamente después del paso "Install build dependencies" (después de la línea 67):

```yaml
      - name: Fix install command
        run: |
          sudo update-alternatives --install /usr/bin/install install /usr/bin/gnuinstall 100
```

## Sección Completa Actualizada

```yaml
      - name: Install build dependencies
        env:
          DEBIAN_FRONTEND: noninteractive
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            build-essential wget cpio unzip rsync bc \
            python3 python3-pip libssl-dev \
            file gawk texinfo bison flex \
            exfatprogs \
            libncurses-dev \
            dosfstools mtools \
            gdisk \
            libconfuse-dev \
            nlohmann-json3-dev \
            cmake \
            squashfs-tools
          wget -q https://github.com/pengutronix/genimage/releases/download/v17/genimage-17.tar.xz
          tar xf genimage-17.tar.xz
          cd genimage-17
          ./configure
          make -j$(nproc)
          sudo make install
          cd ..
          rm -rf genimage-17 genimage-17.tar.xz

      - name: Fix install command
        run: |
          sudo update-alternatives --install /usr/bin/install install /usr/bin/gnuinstall 100

      - name: Cache ccache
```

## Explicación

Este paso asegura que el comando GNU `install` esté correctamente configurado antes de que comience el proceso de compilación, lo que resolverá el error de verificación de dependencias en `support/dependencies/dependencies.mk`.

Al ejecutar `update-alternatives`, estableces `/usr/bin/gnuinstall` como la alternativa predeterminada para `/usr/bin/install`, evitando conflictos con otras versiones de coreutils que puedan estar instaladas en el entorno de compilación.
