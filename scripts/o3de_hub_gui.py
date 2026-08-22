#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#
"""
A small, cross-platform (Windows + Linux) **pre-build** GUI hub for O3DE, built on Tkinter (which
ships with CPython, so it needs no extra packages and runs on a *system* Python before the engine's
bundled venv exists).

It fills the gap that O3DE's own Qt "Project Manager" leaves open: that GUI only exists *after* you
have already built the engine, which is exactly the painful part of onboarding. This hub instead:

  * runs the build-prerequisite "doctor" checks (reusing o3de.hub) as green/red rows, including the
    Windows gotchas that block onboarding - Smart App Control / Device Guard code integrity and the
    missing VC++ redistributable - each with a one-click "fix" link, and
  * creates a project with an explicit **name** + path (so you never end up with an accidental
    "Default Project"), and registers this engine, streaming live output into a log pane.

Launch it with scripts/o3de_hub.bat (Windows) or scripts/o3de_hub.sh (Linux/macOS).
"""

import collections
import os
import pathlib
import re
import subprocess
import sys
import threading
import webbrowser

# This file lives at <engine>/scripts/o3de_hub_gui.py.
ENGINE_ROOT = pathlib.Path(__file__).resolve().parents[1]
# Make the o3de python package importable so we can reuse the doctor checks. o3de.hub is deliberately
# stdlib-only at import time, so this works under a system Python before the bundled venv exists.
sys.path.insert(0, str(ENGINE_ROOT / 'scripts' / 'o3de'))

from o3de import hub  # noqa: E402  (path set up above)

# Mirror of the project-name rules enforced by o3de create-project (engine_template.py), reproduced
# here with the stdlib so name validation works in the GUI before the engine tooling is available.
_PROJECT_NAME_RE = re.compile(r'^[A-Za-z][A-Za-z0-9_-]*$')

# CMake reserves these target names (with CTest enabled); a project so named fails configure with
# 'The target name "test" is reserved'.
_RESERVED_CMAKE_NAMES = {'all', 'all_build', 'clean', 'help', 'install', 'package',
                         'package_source', 'test', 'run_tests', 'edit_cache', 'rebuild_cache',
                         'zero_check', 'list_install_components'}
_URL_RE = re.compile(r'https?://\S+')


def o3de_launcher(engine_root: pathlib.Path) -> pathlib.Path:
    """Path to the platform o3de launcher script."""
    if sys.platform.startswith('win'):
        return engine_root / 'scripts' / 'o3de.bat'
    return engine_root / 'scripts' / 'o3de.sh'


def validate_project_name(name: str) -> str or None:
    """Returns an error string if the project name is invalid, else None."""
    if not name:
        return 'Enter a project name.'
    if len(name) >= 64:
        return 'Project name must be fewer than 64 characters.'
    if not _PROJECT_NAME_RE.match(name):
        return ('Project name must start with a letter and contain only letters, '
                'digits, "_" or "-".')
    if name.lower() in _RESERVED_CMAKE_NAMES:
        return (f'"{name}" is a reserved CMake target name and the project would fail to '
                f'configure - pick another name (e.g. {name.capitalize()}Game).')
    return None


def build_create_project_command(engine_root: pathlib.Path, project_path: pathlib.Path,
                                 project_name: str) -> list:
    return [str(o3de_launcher(engine_root)), 'create-project',
            '--project-path', str(project_path), '--project-name', project_name]


def build_register_engine_command(engine_root: pathlib.Path) -> list:
    return [str(o3de_launcher(engine_root)), 'register', '--this-engine']


def first_url(text: str) -> str or None:
    match = _URL_RE.search(text or '')
    return match.group(0).rstrip('.)') if match else None


def project_build_dir(project_path: pathlib.Path) -> pathlib.Path:
    return project_path / 'build' / ('windows' if sys.platform.startswith('win') else 'linux')


def build_configure_command(project_path: pathlib.Path) -> list:
    """CMake configure for a project, matching the quick-start docs."""
    third_party = str(hub.get_third_party_path() or (pathlib.Path.home() / '.o3de' / '3rdParty'))
    command = ['cmake', '-B', str(project_build_dir(project_path)), '-S', str(project_path),
               f'-DLY_3RDPARTY_PATH={third_party}']
    if sys.platform.startswith('win'):
        generator = hub.detect_windows_generator()
        return command + (['-G', generator] if generator else [])
    return command + ['-G', 'Ninja Multi-Config']


def _uses_visual_studio_generator(build_dir: pathlib.Path) -> bool:
    try:
        cache = (build_dir / 'CMakeCache.txt').read_text(encoding='utf-8', errors='ignore')
    except OSError:
        return False
    return bool(re.search(r'^CMAKE_GENERATOR:\w+=Visual Studio', cache, re.MULTILINE))


def build_build_command(project_path: pathlib.Path, target: str = None, config: str = 'profile') -> list:
    """target=None builds everything (all gems, Editor, launchers)."""
    build_dir = project_build_dir(project_path)
    command = ['cmake', '--build', str(build_dir)]
    if target:
        command += ['--target', target]
    command += ['--config', config]
    if _uses_visual_studio_generator(build_dir):
        # unbounded MSBuild parallelism exhausts RAM on unity MSVC builds (C1060/LNK1102)
        command += ['--'] + hub.memory_safe_msbuild_args()
    return command


def binary_path(project_path: pathlib.Path, name: str, config: str = 'profile') -> pathlib.Path:
    executable = f'{name}.exe' if sys.platform.startswith('win') else name
    return project_build_dir(project_path) / 'bin' / config / executable


def configure_succeeded(project_path: pathlib.Path) -> bool:
    """True when a previous Configure completed (a failed configure leaves CMakeCache.txt behind
    but never writes the generator's build files)."""
    build_dir = project_build_dir(project_path)
    if sys.platform.startswith('win'):
        return bool(list(build_dir.glob('*.sln'))) or (build_dir / 'build.ninja').is_file()
    return (build_dir / 'build-profile.ninja').is_file() or (build_dir / 'build.ninja').is_file() \
        or (build_dir / 'Makefile').is_file()


BUILD_ALL_CHOICE = 'All (gems + launchers)'


def project_gem_names(project_path: pathlib.Path) -> list:
    """Enabled gem names from the project's project.json (each gem's module is a CMake target of
    the same name)."""
    import json
    try:
        data = json.loads((pathlib.Path(project_path) / 'project.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return []
    gems = data.get('gem_names', [])
    names = []
    for gem in gems if isinstance(gems, list) else []:
        # entries are either "GemName" or {"name": "GemName", ...}
        name = gem.get('name') if isinstance(gem, dict) else gem
        if isinstance(name, str) and name:
            names.append(name)
    return names


def build_target_choices(project_path: pathlib.Path) -> list:
    return [BUILD_ALL_CHOICE, 'Editor',
            f'{project_path.name}.GameLauncher', f'{project_path.name}.ServerLauncher'] + \
        sorted(project_gem_names(project_path))


def registered_projects() -> list:
    """Project paths from ~/.o3de/o3de_manifest.json (stdlib-only, works pre-bootstrap)."""
    import json
    manifest_file = pathlib.Path(os.environ.get('USERPROFILE') or pathlib.Path.home()) / '.o3de' / 'o3de_manifest.json'
    try:
        data = json.loads(manifest_file.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return []
    projects = data.get('projects', [])
    return [str(p) for p in projects] if isinstance(projects, list) else []


# Everything below is GUI-only; importing this module for unit tests of the helpers above must not
# require a display, so Tkinter is imported lazily inside main().

_SEVERITY_COLOR = {hub.OK: '#1a7f37', hub.WARN: '#bf8700', hub.FAIL: '#cf222e'}


class HubGui:
    def __init__(self, root, tk_module, ttk_module, filedialog_module, messagebox_module):
        self.tk = tk_module
        self.ttk = ttk_module
        self.filedialog = filedialog_module
        self.messagebox = messagebox_module
        self.root = root
        self._proc_thread = None

        root.title('AIO3DE Hub')
        root.geometry('820x640')

        notebook = self.ttk.Notebook(root)
        notebook.pack(fill='both', expand=True, padx=8, pady=(8, 4))

        self.preflight_frame = self.ttk.Frame(notebook)
        self.project_frame = self.ttk.Frame(notebook)
        self.build_frame = self.ttk.Frame(notebook)
        self.gems_frame = self.ttk.Frame(notebook)
        notebook.add(self.preflight_frame, text='Preflight')
        notebook.add(self.project_frame, text='Create Project')
        notebook.add(self.build_frame, text='Build & Run')
        notebook.add(self.gems_frame, text='Gems')

        self._build_preflight(self.preflight_frame)
        self._build_project(self.project_frame)
        self._build_build_tab(self.build_frame)
        self._build_gems_tab(self.gems_frame)
        self._build_log(root)

        self.run_preflight()

    # ---- Preflight tab -------------------------------------------------------------------------
    def _build_preflight(self, parent):
        top = self.ttk.Frame(parent)
        top.pack(fill='x', padx=8, pady=8)
        self.ttk.Label(top, text='Build prerequisites for this machine',
                       font=('TkDefaultFont', 11, 'bold')).pack(side='left')
        self.ttk.Button(top, text='Re-run checks', command=self.run_preflight).pack(side='right')
        self.install_button = self.ttk.Button(top, text='Install missing',
                                              command=self._install_missing, state='disabled')
        self.install_button.pack(side='right', padx=6)

        columns = ('status', 'check', 'detail')
        self.tree = self.ttk.Treeview(parent, columns=columns, show='headings', height=12)
        self.tree.heading('status', text='Status')
        self.tree.heading('check', text='Check')
        self.tree.heading('detail', text='Detail')
        self.tree.column('status', width=70, anchor='center')
        self.tree.column('check', width=190, anchor='w')
        self.tree.column('detail', width=520, anchor='w')
        self.tree.pack(fill='both', expand=True, padx=8)
        for severity, color in _SEVERITY_COLOR.items():
            self.tree.tag_configure(severity, foreground=color)
        self.tree.bind('<<TreeviewSelect>>', self._on_check_selected)

        hint_bar = self.ttk.Frame(parent)
        hint_bar.pack(fill='x', padx=8, pady=8)
        self.hint_var = self.tk.StringVar(value='Select a check to see how to fix it.')
        self.ttk.Label(hint_bar, textvariable=self.hint_var, wraplength=640,
                       justify='left').pack(side='left', fill='x', expand=True)
        self.fix_button = self.ttk.Button(hint_bar, text='Open fix link', command=self._open_fix_link,
                                          state='disabled')
        self.fix_button.pack(side='right')
        self._selected_url = None
        self._checks = []

    def run_preflight(self):
        for row in self.tree.get_children():
            self.tree.delete(row)
        self.tree.insert('', 'end', values=('...', 'Running checks', 'please wait'))

        def worker():
            engine_path = hub.engine_path_fallback()
            checks = hub.run_doctor(engine_path)
            self.root.after(0, lambda: self._populate_checks(checks))

        threading.Thread(target=worker, daemon=True).start()

    def _populate_checks(self, checks):
        for row in self.tree.get_children():
            self.tree.delete(row)
        self._checks = checks
        for check in checks:
            self.tree.insert('', 'end', values=(check.severity, check.name, check.detail),
                             tags=(check.severity,))
        fails = sum(1 for c in checks if c.severity == hub.FAIL)
        warns = sum(1 for c in checks if c.severity == hub.WARN)
        self.log(f'Preflight: {len(checks)} checks, {fails} failed, {warns} warnings.')
        fixable = hub.build_fix_plan(checks, hub.engine_path_fallback())
        self.install_button.configure(state=('normal' if fixable else 'disabled'))

    def _on_check_selected(self, _event):
        selection = self.tree.selection()
        if not selection:
            return
        index = self.tree.index(selection[0])
        if index >= len(self._checks):
            return
        check = self._checks[index]
        self.hint_var.set(check.hint or 'No action needed.')
        self._selected_url = first_url(check.hint)
        self.fix_button.configure(state=('normal' if self._selected_url else 'disabled'))

    def _open_fix_link(self):
        if self._selected_url:
            webbrowser.open(self._selected_url)

    def _install_missing(self):
        """Runs the doctor's fix plan (system packages via pkexec/sudo, then the per-user steps:
        git lfs, 3rdParty folder, engine Python bootstrap, engine registration)."""
        if self._proc_thread and self._proc_thread.is_alive():
            self.messagebox.showinfo('Busy', 'A command is already running; please wait for it to finish.')
            return
        steps = hub.build_fix_plan(self._checks, hub.engine_path_fallback())
        if not steps:
            self.messagebox.showinfo('Nothing to install', 'All actionable checks are already OK.')
            return
        summary = '\n'.join(f'  - {step.description}' for step in steps)
        if not self.messagebox.askyesno(
                'Install missing prerequisites',
                f'The following will be installed/run (you may get a password prompt for '
                f'system packages):\n\n{summary}\n\nProceed?'):
            return
        self.log('$ Installing missing prerequisites')
        self.install_button.configure(state='disabled')
        self._set_busy(True)

        def worker():
            def gui_log(line):
                self.root.after(0, lambda l=line: self.log(l))
            result = hub.run_fix_plan(steps, gui=True, log=gui_log)
            self.root.after(0, lambda: self.log(f'  (install finished with exit code {result})'))
            self.root.after(0, lambda: self._set_busy(False))
            self.root.after(0, self.run_preflight)

        self._proc_thread = threading.Thread(target=worker, daemon=True)
        self._proc_thread.start()

    # ---- Create Project tab --------------------------------------------------------------------
    def _build_project(self, parent):
        form = self.ttk.Frame(parent)
        form.pack(fill='x', padx=8, pady=8)

        self.ttk.Label(form, text='Project name').grid(row=0, column=0, sticky='w', pady=4)
        self.name_var = self.tk.StringVar()
        self.ttk.Entry(form, textvariable=self.name_var, width=40).grid(row=0, column=1, sticky='w', pady=4)

        self.ttk.Label(form, text='Project location').grid(row=1, column=0, sticky='w', pady=4)
        self.path_var = self.tk.StringVar(value=str(pathlib.Path.home() / 'O3DEProjects'))
        self.ttk.Entry(form, textvariable=self.path_var, width=40).grid(row=1, column=1, sticky='w', pady=4)
        self.ttk.Button(form, text='Browse...', command=self._browse_path).grid(row=1, column=2, padx=4)

        buttons = self.ttk.Frame(parent)
        buttons.pack(fill='x', padx=8, pady=8)
        self.create_button = self.ttk.Button(buttons, text='Create project', command=self._create_project)
        self.create_button.pack(side='left')
        self.register_button = self.ttk.Button(buttons, text='Register this engine',
                                               command=self._register_engine)
        self.register_button.pack(side='left', padx=8)

        self.ttk.Label(parent,
                       text='The project is created under <location>/<name>. The name field is the '
                            'piece the command-line flow does not prompt for, so this avoids the '
                            'accidental "Default Project".',
                       wraplength=760, justify='left', foreground='#57606a').pack(fill='x', padx=8)

    def _browse_path(self):
        chosen = self.filedialog.askdirectory(initialdir=self.path_var.get() or str(pathlib.Path.home()))
        if chosen:
            self.path_var.set(chosen)

    def _create_project(self):
        name = self.name_var.get().strip()
        error = validate_project_name(name)
        if error:
            self.messagebox.showerror('Invalid project name', error)
            return
        location = pathlib.Path(self.path_var.get().strip()).expanduser()
        project_path = location / name
        if project_path.exists() and any(project_path.iterdir()):
            self.messagebox.showerror('Path not empty', f'{project_path} already exists and is not empty.')
            return
        command = build_create_project_command(ENGINE_ROOT, project_path, name)
        self._run_command(command, f'Creating project "{name}" at {project_path}')

    def _register_engine(self):
        self._run_command(build_register_engine_command(ENGINE_ROOT), 'Registering this engine')

    # ---- Build & Run tab -----------------------------------------------------------------------
    def _build_build_tab(self, parent):
        form = self.ttk.Frame(parent)
        form.pack(fill='x', padx=8, pady=8)

        self.ttk.Label(form, text='Project').grid(row=0, column=0, sticky='w', pady=4)
        self.build_project_var = self.tk.StringVar()
        self.project_combo = self.ttk.Combobox(form, textvariable=self.build_project_var, width=52)
        self.project_combo.grid(row=0, column=1, sticky='w', pady=4)
        self.ttk.Button(form, text='Browse...', command=self._browse_build_project).grid(row=0, column=2, padx=4)
        self.ttk.Button(form, text='Refresh list', command=self._refresh_projects).grid(row=0, column=3, padx=4)

        buttons = self.ttk.Frame(parent)
        buttons.pack(fill='x', padx=8, pady=8)
        self.configure_button = self.ttk.Button(buttons, text='1. Configure',
                                                command=self._configure_project)
        self.configure_button.pack(side='left')
        self.build_target_var = self.tk.StringVar(value='Editor')
        self.target_combo = self.ttk.Combobox(buttons, textvariable=self.build_target_var,
                                              width=32, state='readonly')
        self.target_combo.pack(side='left', padx=8)
        self.build_button = self.ttk.Button(buttons, text='2. Build', command=self._build_selected)
        self.build_button.pack(side='left')
        self.ap_button = self.ttk.Button(buttons, text='3. Asset Processor',
                                         command=lambda: self._run_binary('AssetProcessor'))
        self.ap_button.pack(side='left')
        self.editor_button = self.ttk.Button(buttons, text='4. Open Editor',
                                             command=lambda: self._run_binary('Editor'))
        self.editor_button.pack(side='left', padx=8)

        self.ttk.Label(parent,
                       text='Configure once (and again after enabling gems), then pick a build target '
                            '- "Editor", everything, a launcher, or a single gem - and Build. The '
                            'first build downloads 3rd party packages and takes a long time. Asset '
                            'Processor and the Editor launch detached once built.',
                       wraplength=760, justify='left', foreground='#57606a').pack(fill='x', padx=8)
        self.build_project_var.trace_add('write', lambda *_: self._refresh_targets())
        self._refresh_projects()
        self._refresh_targets()

    def _refresh_projects(self):
        projects = registered_projects()
        self.project_combo['values'] = projects
        if projects and not self.build_project_var.get():
            self.build_project_var.set(projects[-1])

    def _refresh_targets(self):
        raw = self.build_project_var.get().strip()
        project_path = pathlib.Path(raw).expanduser() if raw else None
        if project_path and (project_path / 'project.json').is_file():
            choices = build_target_choices(project_path)
        else:
            choices = [BUILD_ALL_CHOICE, 'Editor']
        self.target_combo['values'] = choices
        if self.build_target_var.get() not in choices:
            self.build_target_var.set('Editor')

    def _browse_build_project(self):
        chosen = self.filedialog.askdirectory(initialdir=self.build_project_var.get() or
                                              str(pathlib.Path.home()))
        if chosen:
            self.build_project_var.set(chosen)

    def _selected_project(self) -> pathlib.Path or None:
        raw = self.build_project_var.get().strip()
        if not raw:
            self.messagebox.showerror('No project', 'Select or browse to a project folder first.')
            return None
        project_path = pathlib.Path(raw).expanduser()
        if not (project_path / 'project.json').is_file():
            self.messagebox.showerror('Not a project', f'No project.json found in {project_path}.')
            return None
        return project_path

    def _configure_project(self):
        project_path = self._selected_project()
        if project_path:
            self._run_command(build_configure_command(project_path),
                              f'Configuring {project_path.name}')

    def _build_selected(self):
        choice = self.build_target_var.get()
        if choice == BUILD_ALL_CHOICE:
            self._build_target(None, 'Building everything for {name} - all gems, Editor and '
                                     'launchers (this takes even longer than the Editor build)')
        else:
            self._build_target(choice, f'Building {choice} for {{name}} (this can take a while)')

    def _build_target(self, target, message):
        project_path = self._selected_project()
        if not project_path:
            return
        if not configure_succeeded(project_path):
            self.messagebox.showerror(
                'Configure first',
                'No successful Configure found for this project - run "1. Configure" and make sure '
                'it finishes without errors (see the log) before building.')
            return
        self._run_command(build_build_command(project_path, target),
                          message.format(name=project_path.name))

    def _run_binary(self, name: str):
        project_path = self._selected_project()
        if not project_path:
            return
        executable = binary_path(project_path, name)
        if not executable.is_file():
            self.messagebox.showerror('Not built yet',
                                      f'{executable} does not exist - run Configure and Build first.')
            return
        self.log(f'$ Launching {executable}')
        try:
            subprocess.Popen([str(executable), '--project-path', str(project_path)],
                             cwd=str(executable.parent))
        except OSError as exc:
            self.log(f'  ERROR: could not launch: {exc}')

    # ---- Gems tab ------------------------------------------------------------------------------
    def _build_gems_tab(self, parent):
        form = self.ttk.Frame(parent)
        form.pack(fill='x', padx=8, pady=8)

        self.ttk.Label(form, text='Project').grid(row=0, column=0, sticky='w', pady=4)
        self.gems_project_var = self.tk.StringVar()
        self.gems_project_combo = self.ttk.Combobox(form, textvariable=self.gems_project_var, width=52)
        self.gems_project_combo.grid(row=0, column=1, sticky='w', pady=4)
        self.ttk.Button(form, text='Browse...', command=self._browse_gems_project).grid(row=0, column=2, padx=4)
        self.ttk.Button(form, text='Refresh', command=self._refresh_gems).grid(row=0, column=3, padx=4)

        columns = ('state', 'gem', 'summary')
        self.gems_tree = self.ttk.Treeview(parent, columns=columns, show='headings', height=12)
        self.gems_tree.heading('state', text='State')
        self.gems_tree.heading('gem', text='Gem')
        self.gems_tree.heading('summary', text='Summary')
        self.gems_tree.column('state', width=80, anchor='center')
        self.gems_tree.column('gem', width=220, anchor='w')
        self.gems_tree.column('summary', width=460, anchor='w')
        self.gems_tree.pack(fill='both', expand=True, padx=8)
        self.gems_tree.tag_configure('enabled', foreground='#1a7f37')

        buttons = self.ttk.Frame(parent)
        buttons.pack(fill='x', padx=8, pady=8)
        self.gem_enable_button = self.ttk.Button(buttons, text='Enable selected',
                                                 command=lambda: self._toggle_gem(True))
        self.gem_enable_button.pack(side='left')
        self.gem_disable_button = self.ttk.Button(buttons, text='Disable selected',
                                                  command=lambda: self._toggle_gem(False))
        self.gem_disable_button.pack(side='left', padx=8)
        self.ttk.Label(buttons,
                       text='After enabling or disabling a gem, Configure + Build the project again '
                            '(Build & Run tab).',
                       foreground='#57606a').pack(side='left', padx=8)

        self.gems_project_var.trace_add('write', lambda *_: self._refresh_gems())
        self.gems_project_combo['values'] = registered_projects()
        projects = registered_projects()
        if projects:
            self.gems_project_var.set(projects[-1])

    def _browse_gems_project(self):
        chosen = self.filedialog.askdirectory(initialdir=self.gems_project_var.get() or
                                              str(pathlib.Path.home()))
        if chosen:
            self.gems_project_var.set(chosen)

    def _gems_selected_project(self) -> pathlib.Path or None:
        raw = self.gems_project_var.get().strip()
        if not raw:
            return None
        project_path = pathlib.Path(raw).expanduser()
        return project_path if (project_path / 'project.json').is_file() else None

    def _refresh_gems(self):
        for row in self.gems_tree.get_children():
            self.gems_tree.delete(row)
        project_path = self._gems_selected_project()
        if not project_path:
            return
        enabled = hub.project_enabled_gem_names(project_path)
        known = set()
        for name, _path, summary in hub.available_gems(ENGINE_ROOT):
            known.add(name)
            state = 'enabled' if name in enabled else '-'
            tags = ('enabled',) if name in enabled else ()
            self.gems_tree.insert('', 'end', values=(state, name, summary), tags=tags)
        for name in sorted(enabled - known):
            self.gems_tree.insert('', 'end', values=('enabled', name,
                                                     '(not found among registered gems)'),
                                  tags=('enabled',))

    def _toggle_gem(self, enable: bool):
        project_path = self._gems_selected_project()
        if not project_path:
            self.messagebox.showerror('No project', 'Select or browse to a project folder first.')
            return
        selection = self.gems_tree.selection()
        if not selection:
            self.messagebox.showinfo('No gem selected', 'Select a gem in the list first.')
            return
        gem_name = self.gems_tree.item(selection[0], 'values')[1]
        verb = 'enable-gem' if enable else 'disable-gem'
        command = [str(o3de_launcher(ENGINE_ROOT)), verb, '-gn', gem_name,
                   '-pp', str(project_path)]
        self._run_command(command, f'{"Enabling" if enable else "Disabling"} {gem_name} in '
                                   f'{project_path.name} (Configure + Build again afterwards)',
                          on_done=self._refresh_gems)

    # ---- shared log + command runner -----------------------------------------------------------
    def _build_log(self, parent):
        frame = self.ttk.Frame(parent)
        frame.pack(fill='both', expand=True, padx=8, pady=(4, 8))
        self.ttk.Label(frame, text='Log').pack(anchor='w')
        self.log_text = self.tk.Text(frame, height=10, wrap='word', state='disabled')
        self.log_text.pack(fill='both', expand=True)

    def log(self, line: str):
        self.log_text.configure(state='normal')
        self.log_text.insert('end', line.rstrip('\n') + '\n')
        self.log_text.see('end')
        self.log_text.configure(state='disabled')

    def _set_busy(self, busy: bool):
        state = 'disabled' if busy else 'normal'
        self.create_button.configure(state=state)
        self.register_button.configure(state=state)
        self.configure_button.configure(state=state)
        self.build_button.configure(state=state)

    def _run_command(self, command: list, description: str, on_done=None):
        if self._proc_thread and self._proc_thread.is_alive():
            self.messagebox.showinfo('Busy', 'A command is already running; please wait for it to finish.')
            return
        self.log(f'$ {description}')
        self.log('  ' + ' '.join(command))
        self._set_busy(True)

        def worker():
            try:
                process = subprocess.Popen(command, cwd=str(ENGINE_ROOT), stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, text=True, bufsize=1)
            except OSError as exc:
                self.root.after(0, lambda: self.log(f'  ERROR: could not start command: {exc}'))
                self.root.after(0, lambda: self._set_busy(False))
                return
            output_tail = collections.deque(maxlen=200)
            for line in process.stdout:
                output_tail.append(line)
                self.root.after(0, lambda l=line: self.log('  ' + l.rstrip('\n')))
            process.wait()
            self.root.after(0, lambda: self.log(f'  (exit code {process.returncode})'))
            if process.returncode != 0:
                hint = hub.build_oom_hint(''.join(output_tail))
                if hint:
                    self.root.after(0, lambda: self.log(f'  HINT: {hint}'))
            self.root.after(0, lambda: self._set_busy(False))
            self.root.after(0, self.run_preflight)
            if on_done:
                self.root.after(0, on_done)

        self._proc_thread = threading.Thread(target=worker, daemon=True)
        self._proc_thread.start()


def main() -> int:
    try:
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
    except ImportError:
        sys.stderr.write(
            'Tkinter is not available in this Python. On Linux install it (e.g. "sudo apt install '
            'python3-tk"); on Windows it ships with the standard python.org installer.\n')
        return 1
    if not os.environ.get('DISPLAY') and not sys.platform.startswith('win') and sys.platform != 'darwin':
        sys.stderr.write('No display detected (DISPLAY is unset). The GUI hub needs a desktop session.\n')
        return 1
    root = tk.Tk()
    HubGui(root, tk, ttk, filedialog, messagebox)
    root.mainloop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
