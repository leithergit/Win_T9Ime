// TestHost (C#, WinForms, .NET 3.5+): shows how a .NET application uses T9Ctl.dll.
// Built with the .NET 3.5 C# compiler so it runs on a stock Windows 7 SP1;
// TestHost.CS.exe.config lets it run on .NET 4.x too (Windows 10/11).
using System;
using System.Drawing;
using System.Windows.Forms;

namespace T9Ime
{
    class MainForm : Form
    {
        readonly TextBox text = new TextBox();
        readonly Label status = new Label();
        readonly FlowLayoutPanel buttons = new FlowLayoutPanel();
        uint visibilityMessage;

        MainForm()
        {
            Text = "T9Ime TestHost (C#)";
            Font = new Font(Font.FontFamily, 12f);
            ClientSize = new Size(560, 440);

            buttons.Dock = DockStyle.Top;
            buttons.AutoSize = true;
            AddButton("显示键盘", delegate { T9Ctl.T9_ShowKeyboard(T9Ctl.ModeKeep); });
            AddButton("隐藏键盘", delegate { T9Ctl.T9_HideKeyboard(); });
            AddButton("切换", delegate { T9Ctl.T9_ToggleKeyboard(); });
            AddButton("中文", delegate { T9Ctl.T9_ShowKeyboard(T9Ctl.ModeChinese); });
            AddButton("英文", delegate { T9Ctl.T9_ShowKeyboard(T9Ctl.ModeEnglish); });
            AddButton("数字", delegate { T9Ctl.T9_ShowKeyboard(T9Ctl.ModeNumber); });
            AddButton("符号", delegate { T9Ctl.T9_ShowKeyboard(T9Ctl.ModeSymbol); });
            AddButton("停靠底部", delegate { T9Ctl.T9_SetDock(); });
            AddButton("切到 T9Ime", delegate { T9Ctl.T9_Activate(Handle); });
            AddButton("切走 T9Ime", delegate { T9Ctl.T9_Deactivate(Handle); });

            status.Dock = DockStyle.Bottom;
            status.Height = 28;
            text.Multiline = true;
            text.ScrollBars = ScrollBars.Vertical;
            Controls.Add(text);
            Controls.Add(buttons);
            Controls.Add(status);
            Resize += delegate { UpdateStatus(); };
            Move += delegate { UpdateStatus(); };
        }

        void AddButton(string caption, EventHandler click)
        {
            Button b = new Button();
            b.Text = caption;
            b.AutoSize = true;
            b.Click += click;
            b.Click += delegate { text.Focus(); UpdateStatus(); };
            buttons.Controls.Add(b);
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            try
            {
                visibilityMessage = T9Ctl.T9_GetVisibilityMessage();
                T9Ctl.T9_RegisterVisibilityNotify(Handle);
            }
            catch (DllNotFoundException)
            {
                status.Text = "找不到 T9Ctl.dll（需与本程序同一目录，且与进程位数一致）";
            }
        }

        protected override void OnHandleDestroyed(EventArgs e)
        {
            try { T9Ctl.T9_UnregisterVisibilityNotify(Handle); } catch (DllNotFoundException) { }
            base.OnHandleDestroyed(e);
        }

        protected override void WndProc(ref Message m)
        {
            if (visibilityMessage != 0 && (uint)m.Msg == visibilityMessage)
            {
                UpdateStatus();
                return;
            }
            base.WndProc(ref m);
        }

        static readonly string[] Modes = { "未运行", "中文", "英文", "数字", "符号" };

        // Status line, and keep the text box above the keyboard.
        void UpdateStatus()
        {
            if (visibilityMessage == 0) return;
            bool visible = T9Ctl.T9_IsKeyboardVisible();
            T9Ctl.RECT kb;
            T9Ctl.T9_GetKeyboardRect(out kb);
            int mode = T9Ctl.T9_GetMode();
            status.Text = string.Format("键盘：{0}，布局：{1}，位置 {2},{3} 大小 {4}×{5}{6}",
                visible ? "显示" : "隐藏", Modes[mode >= 0 && mode <= 4 ? mode : 0], kb.Left, kb.Top,
                kb.Right - kb.Left, kb.Bottom - kb.Top, T9Ctl.T9_IsInstalled() ? "" : "（T9Ime 未安装）");

            int top = buttons.Bottom;
            int bottom = status.Top;
            if (visible)
            {
                Rectangle client = RectangleToScreen(ClientRectangle);
                Point kbTop = PointToClient(new Point(kb.Left, kb.Top));
                bool overlaps = kb.Left < client.Right && kb.Right > client.Left;
                if (overlaps && kbTop.Y > top && kbTop.Y < bottom) bottom = kbTop.Y - 4;
            }
            text.Dock = DockStyle.None;
            text.SetBounds(4, top + 4, ClientSize.Width - 8, Math.Max(20, bottom - top - 8));
        }

        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.Run(new MainForm());
        }
    }
}
