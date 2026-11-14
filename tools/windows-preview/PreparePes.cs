using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Forms;

internal sealed class PreparePes : Form
{
    private static readonly Color Background = Color.FromArgb(17, 17, 21);
    private static readonly Color Field = Color.FromArgb(36, 36, 43);
    private static readonly Color Foreground = Color.FromArgb(246, 244, 241);
    private static readonly Color Muted = Color.FromArgb(191, 190, 198);
    private static readonly Color Accent = Color.FromArgb(255, 93, 82);
    private static readonly Regex ReleaseTag = new Regex(@"\Av[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?\z", RegexOptions.CultureInvariant);
    private readonly string root;
    private readonly TableLayoutPanel layout;
    private readonly Label title;
    private readonly Label description;
    private readonly TableLayoutPanel source;
    private readonly Label preparationHint;
    private readonly TextBox isoField;
    private readonly Button select;
    private readonly Button action;
    private readonly Button update;
    private readonly ProgressBar progress;
    private readonly Label status;
    private readonly TextBox logField;
    private readonly Button openLog;
    private string iso;
    private string availableRelease;
    private bool working;
    private bool ready;
    private bool localUpgrade;

    [STAThread]
    private static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        if (!Environment.Is64BitOperatingSystem)
        {
            MessageBox.Show("PES 2013 necesita Windows de 64 bits.", "PES 2013", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        var root = AppDomain.CurrentDomain.BaseDirectory;
        var updateRequested = args.Length == 1 && string.Equals(args[0], "--actualizar", StringComparison.OrdinalIgnoreCase);
        if (!updateRequested && IsReady(root) && !NeedsLocalUpgrade(root))
        {
            try
            {
                LaunchGame(root);
                return;
            }
            catch (Win32Exception error)
            {
                MessageBox.Show("No se pudo abrir PES 2013.\n\n" + error.Message, "PES 2013",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        Application.Run(new PreparePes(root, updateRequested));
    }

    private PreparePes(string directory, bool updateRequested)
    {
        root = directory;
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleDimensions = new SizeF(96, 96);
        AutoScaleMode = AutoScaleMode.Dpi;
        MinimumSize = new Size(560, 400);
        ClientSize = new Size(650, 380);
        BackColor = Background;
        ForeColor = Foreground;
        Font = new Font("Segoe UI", 10);

        layout = new TableLayoutPanel { ColumnCount = 1, RowCount = 8, Dock = DockStyle.Fill, Padding = new Padding(24, 18, 24, 18) };
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 22));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 49));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 47));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 46));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 18));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
        Controls.Add(layout);

        var heading = MakeLabel("PES 2013 PARA WINDOWS", Accent);
        heading.Font = new Font(Font, FontStyle.Bold);
        layout.Controls.Add(heading, 0, 0);
        title = MakeLabel(string.Empty, Foreground);
        title.Font = new Font("Segoe UI", 25, FontStyle.Bold);
        layout.Controls.Add(title, 0, 1);
        description = MakeLabel(string.Empty, Muted);
        layout.Controls.Add(description, 0, 2);

        source = MakeFieldRow(148);
        isoField = MakeField("Archivo ISO elegido");
        select = MakeButton("Elegir ISO…", false);
        select.Click += delegate { SelectIso(); };
        source.Controls.Add(isoField, 0, 0);
        source.Controls.Add(select, 1, 0);
        layout.Controls.Add(source, 0, 3);

        var actions = MakeFieldRow(148);
        var secondary = new Panel { Dock = DockStyle.Fill, Margin = Padding.Empty, Padding = new Padding(0, 3, 0, 5) };
        preparationHint = MakeLabel("La primera preparación puede tardar varios minutos.", Muted);
        update = MakeButton("Buscar actualizaciones", false);
        update.Width = 196;
        update.Dock = DockStyle.Left;
        update.Click += RunUpdate;
        secondary.Controls.Add(preparationHint);
        secondary.Controls.Add(update);
        actions.Controls.Add(secondary, 0, 0);
        action = MakeButton("Preparar", true);
        action.Click += RunAction;
        actions.Controls.Add(action, 1, 0);
        layout.Controls.Add(actions, 0, 4);

        progress = new ProgressBar { Dock = DockStyle.Fill, Margin = new Padding(0, 3, 0, 2), AccessibleName = "Progreso" };
        layout.Controls.Add(progress, 0, 5);
        status = MakeLabel(string.Empty, Muted);
        status.AccessibleName = "Estado";
        layout.Controls.Add(status, 0, 6);

        var logs = MakeFieldRow(148);
        logField = MakeField("Ubicación del registro");
        logField.Visible = false;
        openLog = MakeButton("Abrir registro", false);
        openLog.Visible = false;
        openLog.Click += delegate { OpenLog(); };
        logs.Controls.Add(logField, 0, 0);
        logs.Controls.Add(openLog, 1, 0);
        layout.Controls.Add(logs, 0, 7);
        bool installed = IsReady(root);
        localUpgrade = !updateRequested && installed && NeedsLocalUpgrade(root);
        ShowReadyMode(installed && !localUpgrade);
        if (ready && updateRequested)
            Shown += RunUpdate;
    }

    private void ShowReadyMode(bool isReady)
    {
        ready = isReady;
        Text = ready ? "Actualizaciones de PES 2013" : localUpgrade ? "Actualizá PES 2013" : "Prepará PES 2013";
        title.Text = ready ? "Actualizaciones" : Text;
        description.Text = ready ? "Buscá nuevas versiones de PES 2013 para Windows." : localUpgrade ?
            "Actualizá esta instalación con los archivos del juego que ya tenés." :
            "Elegí tu ISO original de Xbox 360, edición Europa (Español y Portugués).";
        source.Visible = !ready && !localUpgrade;
        layout.RowStyles[3].Height = !ready && !localUpgrade ? 44 : 0;
        preparationHint.Visible = !ready;
        preparationHint.Text = localUpgrade ? "Se van a compilar los cambios de esta versión." :
            "La primera preparación puede tardar varios minutos.";
        update.Visible = ready;
        action.Text = ready ? "Jugar" : localUpgrade ? "Actualizar juego" : "Preparar";
        action.Enabled = ready || localUpgrade || !string.IsNullOrEmpty(iso);
        AcceptButton = action.Enabled ? action : select;
        SetStatus(ready ? "PES 2013 está listo." : localUpgrade ? "La actualización está lista para instalar." :
            "Esperando el archivo ISO.", Muted);
    }

    private static Label MakeLabel(string text, Color color)
    {
        return new Label { Text = text, ForeColor = color, TextAlign = ContentAlignment.MiddleLeft,
            Dock = DockStyle.Fill, Margin = Padding.Empty, UseMnemonic = false };
    }

    private static TableLayoutPanel MakeFieldRow(int buttonWidth)
    {
        var row = new TableLayoutPanel { ColumnCount = 2, RowCount = 1, Dock = DockStyle.Fill, Margin = Padding.Empty };
        row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        row.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, buttonWidth));
        row.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        return row;
    }

    private static TextBox MakeField(string label)
    {
        return new TextBox { ReadOnly = true, BorderStyle = BorderStyle.FixedSingle, AccessibleName = label,
            BackColor = Field, ForeColor = Foreground, Dock = DockStyle.Fill, Margin = new Padding(0, 10, 12, 0) };
    }

    private static Button MakeButton(string label, bool primary)
    {
        var button = new Button { Text = label, BackColor = primary ? Accent : Field,
            ForeColor = primary ? Background : Foreground, FlatStyle = FlatStyle.Flat, Dock = DockStyle.Fill,
            Margin = new Padding(0, 3, 0, 5), UseVisualStyleBackColor = false };
        button.FlatAppearance.BorderColor = primary ? Accent : Muted;
        return button;
    }

    private void SelectIso()
    {
        using (var file = new OpenFileDialog { Title = "Elegí la ISO de PES 2013 (Xbox 360)",
            Filter = "Imágenes de disco (*.iso)|*.iso", CheckFileExists = true, RestoreDirectory = true })
        {
            if (file.ShowDialog(this) != DialogResult.OK)
                return;
            iso = Path.GetFullPath(file.FileName);
        }
        isoField.Text = iso;
        select.Text = "Cambiar ISO…";
        if (!ready)
        {
            action.Text = "Preparar";
            action.Enabled = true;
            AcceptButton = action;
            SetStatus("ISO elegida. Presioná Preparar para continuar.", Muted);
        }
    }

    private async void RunAction(object sender, EventArgs e)
    {
        if (ready)
        {
            try
            {
                LaunchGame(root);
                Close();
            }
            catch (Win32Exception error)
            {
                SetStatus("No se pudo abrir PES 2013. " + error.Message, Accent);
            }
            return;
        }
        if (working || (!localUpgrade && string.IsNullOrEmpty(iso)))
            return;
        if (localUpgrade && IsGameRunning())
        {
            SetStatus("Cerrá PES 2013 antes de actualizar el juego.", Accent);
            return;
        }
        if (MessageBox.Show(this, "Si faltan las herramientas de Microsoft para preparar el juego, se descargarán e instalarán. Windows puede pedirte permiso.\n\n¿Querés continuar?",
            "Preparar PES 2013", MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK)
            return;

        bool upgrading = localUpgrade;
        BeginWork(upgrading ? "Actualizando el juego…" : "Iniciando la preparación…");
        var report = new Progress<string>(ReportStatus);
        var log = new Progress<string>(ShowLog);
        string failure;
        try
        {
            failure = await Task.Run(() => Prepare(iso, upgrading, report, log));
        }
        catch (Exception error)
        {
            failure = (upgrading ? "No se pudo iniciar la actualización. " : "No se pudo iniciar la preparación. ") + error.Message;
        }
        if (failure == null)
            localUpgrade = false;
        ShowReadyMode(failure == null);
        FinishWork(ready);
        if (!ready)
            action.Text = "Reintentar";
        action.Focus();
        SetStatus(ready ? "PES 2013 está listo. Presioná Jugar para abrirlo." : failure, ready ? Foreground : Accent);
    }

    private async void RunUpdate(object sender, EventArgs e)
    {
        if (!ready || working)
            return;
        string release = availableRelease;
        bool applying = release != null;
        if (applying && IsGameRunning())
        {
            SetStatus("Cerrá PES 2013 y volvé a presionar Instalar actualización.", Accent);
            return;
        }

        BeginWork(applying ? "Iniciando la actualización…" : "Buscando actualizaciones en GitHub…");
        var report = new Progress<string>(ReportStatus);
        var log = new Progress<string>(ShowLog);
        ScriptResult result;
        try
        {
            var options = applying ? "-Apply -TargetTag " + release : "-Check";
            result = await Task.Run(() => RunScript("Actualizar PES 2013.ps1", options,
                applying ? "actualizacion" : "busqueda", "No se pudo completar la actualización.", report, log));
        }
        catch (Exception error)
        {
            result = new ScriptResult { Error = "No se pudo iniciar la actualización. " + error.Message };
        }

        if (result.Error == null && !applying)
        {
            if (result.Update == "CURRENT")
                availableRelease = null;
            else if (result.Update != null && result.Update.Length <= 80 && ReleaseTag.IsMatch(result.Update))
                availableRelease = result.Update;
            else
                result.Error = "La búsqueda no devolvió una versión válida. Abrí el registro para ver el detalle.";
        }
        if (applying)
        {
            availableRelease = null;
            if (result.Error == null && !IsReady(root))
                result.Error = "La actualización terminó, pero faltan archivos del juego. Abrí el registro para ver el detalle.";
        }
        FinishWork(result.Error == null);
        update.Text = availableRelease == null ? "Buscar actualizaciones" : "Instalar actualización";
        if (result.Error != null)
            SetStatus(result.Error, Accent);
        else if (applying)
            SetStatus("Actualización instalada. Ya podés abrir PES 2013.", Foreground);
        else if (availableRelease != null)
            SetStatus("La versión " + availableRelease + " está disponible.", Foreground);
        else
            SetStatus("Ya tenés instalada la última versión.", Foreground);
    }

    private void BeginWork(string message)
    {
        working = true;
        select.Enabled = action.Enabled = update.Enabled = false;
        progress.Style = ProgressBarStyle.Marquee;
        progress.MarqueeAnimationSpeed = 30;
        logField.Clear();
        logField.Hide();
        openLog.Hide();
        SetStatus(message, Foreground);
    }

    private void FinishWork(bool success)
    {
        working = false;
        progress.Style = ProgressBarStyle.Blocks;
        progress.Value = success ? 100 : 0;
        select.Enabled = !ready && !localUpgrade;
        action.Enabled = ready || localUpgrade || !string.IsNullOrEmpty(iso);
        update.Enabled = ready;
    }

    private void ReportStatus(string message)
    {
        if (working && message.Length != 0)
            SetStatus(message, Foreground);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (working && e.CloseReason == CloseReason.UserClosing)
        {
            e.Cancel = true;
            MessageBox.Show(this, "Hay una operación en curso. Podés minimizar esta ventana mientras termina.",
                "PES 2013", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        base.OnFormClosing(e);
    }

    private void SetStatus(string text, Color color)
    {
        status.Text = text;
        status.ForeColor = color;
    }

    private void ShowLog(string path)
    {
        logField.Text = path;
        logField.Show();
        openLog.Show();
    }

    private void OpenLog()
    {
        try
        {
            using (Process.Start(Path.Combine(Environment.SystemDirectory, "notepad.exe"), Quoted(logField.Text))) { }
        }
        catch (Win32Exception error)
        {
            SetStatus("No se pudo abrir el registro. " + error.Message, Accent);
        }
    }

    private string Prepare(string source, bool upgrading, IProgress<string> report, IProgress<string> log)
    {
        var support = Path.Combine(root, "Soporte");
        var script = Path.Combine(support, "Compilar PES 2013.ps1");
        if (!File.Exists(script))
            return "Falta un archivo de preparación. Extraé todos los archivos del ZIP en la misma carpeta.";

        using (var buildLock = LockPackage(support))
        {
            if (buildLock == null)
                return "Ya hay otra preparación abierta en esta carpeta. Esperá a que termine.";
            if (IsReady(root) && !NeedsLocalUpgrade(root))
                return null;
            var result = RunScript("Compilar PES 2013.ps1", upgrading ? "-Incremental" : "-Iso " + Quoted(source),
                upgrading ? "actualizacion-local" : "preparacion", upgrading ? "La actualización se interrumpió." :
                "La preparación se interrumpió.", report, log);
            if (result.Error != null)
                return result.Error;
            if (upgrading && NeedsLocalUpgrade(root))
                return "No se pudo confirmar la versión compilada. Abrí el registro y volvé a intentar.";
            return IsReady(root) ? null : "La preparación terminó, pero faltan archivos del juego. Abrí el registro y volvé a intentar.";
        }
    }

    private sealed class ScriptResult
    {
        public string Error;
        public string Update;
    }

    private ScriptResult RunScript(string filename, string options, string logPrefix, string fallbackError,
        IProgress<string> report, IProgress<string> log)
    {
        var support = Path.Combine(root, "Soporte");
        var script = Path.Combine(support, filename);
        if (!File.Exists(script))
            return new ScriptResult { Error = "Falta " + filename + " en la carpeta Soporte." };

        var logDirectory = Path.Combine(support, "Registros");
        Directory.CreateDirectory(logDirectory);
        var logPath = Path.Combine(logDirectory, logPrefix + "-" + DateTime.Now.ToString("yyyyMMdd-HHmmssfff") + "-" +
            Process.GetCurrentProcess().Id + ".log");
        using (var writer = new StreamWriter(logPath, false, new UTF8Encoding(true)) { AutoFlush = true })
        using (var process = new Process())
        {
            log.Report(logPath);
            var result = new ScriptResult();
            var gate = new object();
            process.StartInfo = new ProcessStartInfo
            {
                FileName = Path.Combine(Environment.SystemDirectory, "WindowsPowerShell", "v1.0", "powershell.exe"),
                Arguments = "-NoProfile -STA -ExecutionPolicy Bypass -File " + Quoted(script) + " " + options + " -Gui",
                WorkingDirectory = root,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8
            };
            DataReceivedEventHandler receive = (sender, args) =>
            {
                if (args.Data == null)
                    return;
                lock (gate)
                {
                    try { writer.WriteLine(args.Data); }
                    catch (IOException) { }
                    if (args.Data.StartsWith("PES_STATUS:", StringComparison.Ordinal))
                        report.Report(args.Data.Substring("PES_STATUS:".Length).Trim());
                    else if (args.Data.StartsWith("PES_ERROR:", StringComparison.Ordinal))
                        result.Error = args.Data.Substring("PES_ERROR:".Length).Trim();
                    else if (args.Data.StartsWith("PES_UPDATE:", StringComparison.Ordinal))
                        result.Update = args.Data.Substring("PES_UPDATE:".Length).Trim();
                }
            };
            process.OutputDataReceived += receive;
            process.ErrorDataReceived += receive;
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            process.WaitForExit();
            if (string.IsNullOrEmpty(result.Error))
                result.Error = process.ExitCode == 0 ? null : fallbackError + " Abrí el registro para ver el detalle.";
            return result;
        }
    }

    private bool IsGameRunning()
    {
        bool found = false;
        foreach (var game in Process.GetProcessesByName("Jugar PES 2013"))
        {
            using (game)
            {
                try
                {
                    found |= string.Equals(game.MainModule.FileName, Path.Combine(root, "Jugar PES 2013.exe"),
                        StringComparison.OrdinalIgnoreCase);
                }
                catch (Win32Exception) { }
                catch (InvalidOperationException) { }
            }
        }
        return found;
    }

    private static FileStream LockPackage(string directory)
    {
        try
        {
            return new FileStream(Path.Combine(directory, ".preparando.lock"), FileMode.OpenOrCreate,
                FileAccess.ReadWrite, FileShare.None, 4096, FileOptions.DeleteOnClose);
        }
        catch (IOException error)
        {
            if ((error.HResult & 0xffff) == 32)
                return null;
            throw;
        }
    }

    private static string Quoted(string path)
    {
        // All arguments are existing file paths. Windows file names cannot contain double quotes.
        return "\"" + path + "\"";
    }

    private static bool IsReady(string directory)
    {
        return HasFile(Path.Combine(directory, "Jugar PES 2013.exe")) &&
            (HasGameData(Path.Combine(directory, "game")) ||
                HasGameData(Path.Combine(directory, "PES13Recomp", "game")));
    }

    private static bool NeedsLocalUpgrade(string directory)
    {
        var support = Path.Combine(directory, "Soporte");
        var packaged = ReadVersion(Path.Combine(support, "version.txt"));
        return !string.IsNullOrEmpty(packaged) &&
            !string.Equals(packaged, ReadVersion(Path.Combine(support, "version-compilada.txt")), StringComparison.Ordinal);
    }

    private static string ReadVersion(string path)
    {
        try { return File.ReadAllText(path).Trim(); }
        catch (IOException) { return null; }
        catch (UnauthorizedAccessException) { return null; }
    }

    private static bool HasGameData(string directory)
    {
        return HasFile(Path.Combine(directory, "xex", "default.xex")) &&
            HasFile(Path.Combine(directory, "files", "dt00_s.img"));
    }

    private static bool HasFile(string path)
    {
        try { return new FileInfo(path).Length > 0; }
        catch (IOException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }

    private static void LaunchGame(string directory)
    {
        using (Process.Start(new ProcessStartInfo(Path.Combine(directory, "Jugar PES 2013.exe"))
        {
            WorkingDirectory = directory, UseShellExecute = false, CreateNoWindow = true
        })) { }
    }
}
