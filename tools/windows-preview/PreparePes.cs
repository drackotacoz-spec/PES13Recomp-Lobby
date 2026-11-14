using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

internal sealed class PreparePes : Form
{
    private static readonly Color Background = Color.FromArgb(17, 17, 21);
    private static readonly Color Field = Color.FromArgb(36, 36, 43);
    private static readonly Color Foreground = Color.FromArgb(246, 244, 241);
    private static readonly Color Muted = Color.FromArgb(191, 190, 198);
    private static readonly Color Accent = Color.FromArgb(255, 93, 82);
    private readonly string root;
    private readonly TextBox isoField;
    private readonly Button select;
    private readonly Button action;
    private readonly ProgressBar progress;
    private readonly Label status;
    private readonly TextBox logField;
    private readonly Button openLog;
    private string iso;
    private bool preparing;
    private bool ready;

    [STAThread]
    private static void Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        if (!Environment.Is64BitOperatingSystem)
        {
            MessageBox.Show("PES 2013 necesita Windows de 64 bits.", "PES 2013", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        var root = AppDomain.CurrentDomain.BaseDirectory;
        if (IsReady(root))
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
        Application.Run(new PreparePes(root));
    }

    private PreparePes(string directory)
    {
        root = directory;
        ready = IsReady(root);
        Text = "Prepará PES 2013";
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleDimensions = new SizeF(96, 96);
        AutoScaleMode = AutoScaleMode.Dpi;
        MinimumSize = new Size(560, 400);
        ClientSize = new Size(650, 380);
        BackColor = Background;
        ForeColor = Foreground;
        Font = new Font("Segoe UI", 10);

        var layout = new TableLayoutPanel { ColumnCount = 1, RowCount = 8, Dock = DockStyle.Fill, Padding = new Padding(24, 18, 24, 18) };
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
        var title = MakeLabel("Prepará PES 2013", Foreground);
        title.Font = new Font("Segoe UI", 25, FontStyle.Bold);
        layout.Controls.Add(title, 0, 1);
        layout.Controls.Add(MakeLabel("Elegí tu ISO original de Xbox 360, edición Europa (Español y Portugués).", Muted), 0, 2);

        var source = MakeFieldRow(148);
        isoField = MakeField("Archivo ISO elegido");
        select = MakeButton("Elegir ISO…", false);
        select.Click += delegate { SelectIso(); };
        source.Controls.Add(isoField, 0, 0);
        source.Controls.Add(select, 1, 0);
        layout.Controls.Add(source, 0, 3);

        var actions = MakeFieldRow(148);
        actions.Controls.Add(MakeLabel("La primera preparación puede tardar varios minutos.", Muted), 0, 0);
        action = MakeButton(ready ? "Jugar" : "Preparar", true);
        action.Enabled = ready;
        action.Click += RunAction;
        actions.Controls.Add(action, 1, 0);
        layout.Controls.Add(actions, 0, 4);
        AcceptButton = ready ? action : select;

        progress = new ProgressBar { Dock = DockStyle.Fill, Margin = new Padding(0, 3, 0, 2), AccessibleName = "Progreso de la preparación" };
        layout.Controls.Add(progress, 0, 5);
        status = MakeLabel(ready ? "PES 2013 está listo." : "Esperando el archivo ISO.", Muted);
        status.AccessibleName = "Estado de la preparación";
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
        if (preparing || string.IsNullOrEmpty(iso))
            return;
        if (MessageBox.Show(this, "Si faltan las herramientas de Microsoft para preparar el juego, se descargarán e instalarán. Windows puede pedirte permiso.\n\n¿Querés continuar?",
            "Preparar PES 2013", MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK)
            return;

        preparing = true;
        select.Enabled = false;
        action.Enabled = false;
        progress.Style = ProgressBarStyle.Marquee;
        progress.MarqueeAnimationSpeed = 30;
        logField.Clear();
        logField.Hide();
        openLog.Hide();
        SetStatus("Iniciando la preparación…", Foreground);
        var report = new Progress<string>(message => { if (preparing) SetStatus(message, Foreground); });
        var log = new Progress<string>(ShowLog);
        string failure;
        try
        {
            failure = await Task.Run(() => Prepare(iso, report, log));
        }
        catch (Exception error)
        {
            failure = "No se pudo iniciar la preparación. " + error.Message;
        }
        preparing = false;
        ready = failure == null;
        progress.Style = ProgressBarStyle.Blocks;
        progress.Value = ready ? 100 : 0;
        select.Enabled = !ready;
        action.Text = ready ? "Jugar" : "Reintentar";
        action.Enabled = true;
        action.Focus();
        SetStatus(ready ? "PES 2013 está listo. Presioná Jugar para abrirlo." : failure, ready ? Foreground : Accent);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (preparing && e.CloseReason == CloseReason.UserClosing)
        {
            e.Cancel = true;
            MessageBox.Show(this, "PES 2013 se está preparando. Podés minimizar esta ventana mientras termina.",
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

    private string Prepare(string source, IProgress<string> report, IProgress<string> log)
    {
        var support = Path.Combine(root, "Soporte");
        var script = Path.Combine(support, "Compilar PES 2013.ps1");
        if (!File.Exists(script))
            return "Falta un archivo de preparación. Extraé todos los archivos del ZIP en la misma carpeta.";

        var logDirectory = Path.Combine(support, "Registros");
        Directory.CreateDirectory(logDirectory);
        var logPath = Path.Combine(logDirectory, "preparacion-" + DateTime.Now.ToString("yyyyMMdd-HHmmssfff") + "-" +
            Process.GetCurrentProcess().Id + ".log");
        using (var buildLock = LockPackage(support))
        {
            if (buildLock == null)
                return "Ya hay otra preparación abierta en esta carpeta. Esperá a que termine.";
            if (IsReady(root))
                return null;
            using (var writer = new StreamWriter(logPath, false, new UTF8Encoding(true)) { AutoFlush = true })
            using (var process = new Process())
            {
                log.Report(logPath);
                string errorText = null;
                var gate = new object();
                process.StartInfo = new ProcessStartInfo
                {
                    FileName = Path.Combine(Environment.SystemDirectory, "WindowsPowerShell", "v1.0", "powershell.exe"),
                    Arguments = "-NoProfile -STA -ExecutionPolicy Bypass -File " + Quoted(script) + " -Iso " + Quoted(source) + " -Gui",
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
                            errorText = args.Data.Substring("PES_ERROR:".Length).Trim();
                    }
                };
                process.OutputDataReceived += receive;
                process.ErrorDataReceived += receive;
                process.Start();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();
                process.WaitForExit();
                if (process.ExitCode != 0)
                    return string.IsNullOrEmpty(errorText) ? "La preparación se interrumpió. Abrí el registro para ver el detalle." : errorText;
            }
        }
        return IsReady(root) ? null : "La preparación terminó, pero faltan archivos del juego. Abrí el registro y volvé a intentar.";
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
