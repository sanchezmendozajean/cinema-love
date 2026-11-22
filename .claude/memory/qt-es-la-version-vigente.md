---
name: qt-es-la-version-vigente
description: Qt 6 es la única versión vigente de CinemaLove; Electron y Flutter están obsoletas, archivadas en .7z y nunca son dependencia
metadata:
  type: project
---

`qt6-version/` es la única versión que cuenta. `electron-version` y `flutter-version` están obsoletas, archivadas como `.7z` en la raíz `d:\Coding\CinemaLove`, y son solo referencia histórica: **jamás** dependencia del programa ni lugar donde trabajar.

**Why:** el usuario ya lo había explicado varias veces. El CLAUDE.md de la raíz decía que Electron era la principal y llevó a editar Electron por error (la petición de recordar el tamaño de ventana) — ver [[fase-0-spike]] para la decisión de retirar Electron/Flutter.

**How to apply:** ante cualquier petición sobre «el programa/la app/la ventana», trabajar en `qt6-version/` sin preguntar. No abrir ni modificar los `.7z`. Si algún documento sigue presentando Electron como principal, corregirlo.
