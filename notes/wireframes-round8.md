# Wireframes for round 8 (draft, 2026-11-05)

Rough layout only, based on the round 7 answers (sidebar per stage, Overview first, "Personas" for bundles).

## 1. Overview (start page)

```
┌────────────────┬──────────────────────────────────────────────────────────────┐
│ Personae       │  Current look                         [Apply a Persona ▾]    │
│                │                                                              │
│ ▸ Overview     │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐        │
│   Boot Menu    │  │ BOOT MENU    │→ │ BOOT SPLASH  │→ │ PLASMA SPLASH│        │
│   Boot Splash  │  │ [thumbnail]  │  │ [thumbnail]  │  │ [thumbnail]  │        │
│   Plasma Splash│  │ hollow-grub  │  │ fallout      │  │ Overload     │        │
│   Personas     │  │ ✓ as applied │  │ ⚠ 4 of 5     │  │ ✓ as applied │        │
│   Assets       │  │ Timeout 20 s │  │   kernels    │  │              │        │
│   History      │  │ [Change…]    │  │   outdated   │  │ [Change…]    │        │
│                │  └──────────────┘  └──────────────┘  └──────────────┘        │
│                │                                                              │
│                │  Active Persona: none (mixed)       Last apply: —            │
│ ─────────────  │  System: initramfs-tools · GRUB 2.14 · splash on cmdline ✓   │
│   Settings     │                                                              │
│   About        │                                                              │
└────────────────┴──────────────────────────────────────────────────────────────┘
```

## 2. Stage page (Boot Splash; Boot Menu and Plasma Splash look the same)

```
┌────────────────┬──────────────────────────────────────────────────────────────┐
│ ...sidebar...  │  Boot Splash            [New design ▾] [Import…]  [Search ]  │
│                │                                                              │
│                │  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐                 │
│                │  │ thumb  │ │ thumb  │ │ thumb  │ │ thumb  │                 │
│                │  │fallout │ │mint-gal│ │ Neon   │ │ Calm   │                 │
│                │  │●applied│ │imported│ │ design │ │ design │                 │
│                │  └────────┘ └────────┘ └────────┘ └────────┘                 │
│                │                                                              │
│                │  Selected: Neon (design, saved 5 min ago)                    │
│                │  [Edit] [Preview] [Apply…] [Duplicate] [Delete]              │
└────────────────┴──────────────────────────────────────────────────────────────┘
```

Imported themes offer Preview, Apply and Duplicate (as a Personae design only if Personae generated it); no Edit.

## 3. Editor (opens in the window, sidebar collapses)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ← Neon ●unsaved     [Undo][Redo]   1440p|1080p|720p   [Save] [Versions] [Apply…]│
├───────────┬────────────────────────────────────────────────┬─────────────────┤
│ Elements  │                                                │ Properties      │
│ Background│          ┌───────────────────────────┐         │ Logo            │
│ Logo    ◉ │          │                           │         │ X  50 %  Y 40 % │
│ Spinner   │          │         [ LOGO ]          │         │ W  20 %  (H auto)│
│ Progress  │          │                           │         │ Anchor: centre  │
│ Text      │          │        ◌ spinner          │         │ Image: neon.svg │
│           │          │                           │         │  [Choose asset] │
│ [+ Add]   │          └───────────────────────────┘         │ Opacity 100 %   │
│           │   ⚠ 720p: text box clips 12 px                 │ Shared: Brand ⛓ │
├───────────┴────────────────────────────────────────────────┴─────────────────┤
│ Mode: Boot | Shutdown | Reboot | Password     Warnings (1)                   │
└──────────────────────────────────────────────────────────────────────────────┘
```

Drag on the canvas with snapping (centre, edges, other elements); numbers and arrow keys for precision.

## 4. Apply review (takes over the window)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ Apply Persona "Night"                                                        │
│                                                                              │
│  1. Plasma splash  Overload → Night-KS           no password needed          │
│  2. Boot menu      hollow-grub → Night-GRUB      writes zz-personae.cfg,     │
│                                                  runs update-grub            │
│  3. Boot splash    fallout → Night-PLY           rebuilds 5 initrds (~40 s), │
│                                                  +~640 MB in Timeshift       │
│                                                                              │
│  Checks: ✓ files valid  ✓ 34 GB free  ✓ no apt running  ✓ fallback kept     │
│  ▸ Show file changes and commands                                            │
│                                                                              │
│                                    [Cancel]   [Apply (asks for password)]    │
└──────────────────────────────────────────────────────────────────────────────┘
```

Progress then replaces the buttons. On failure everything is rolled back and the log is shown.
