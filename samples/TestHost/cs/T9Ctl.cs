// P/Invoke declarations for T9Ctl.dll (see src/ctl/t9ctl.h).
// Put the T9Ctl.dll matching the process architecture next to the executable
// (x64 for AnyCPU on 64-bit Windows, x86 otherwise).
using System;
using System.Runtime.InteropServices;

namespace T9Ime
{
    public static class T9Ctl
    {
        public const int ModeKeep = 0;
        public const int ModeChinese = 1;
        public const int ModeEnglish = 2;
        public const int ModeNumber = 3;
        public const int ModeSymbol = 4;

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT
        {
            public int Left, Top, Right, Bottom;
        }

        const string Dll = "T9Ctl.dll";

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_IsInstalled();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_Activate(IntPtr hwnd);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_Deactivate(IntPtr hwnd);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_ShowKeyboard(int mode);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_HideKeyboard();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_ToggleKeyboard();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_IsKeyboardVisible();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_SetMode(int mode);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int T9_GetMode();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_SetDock();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_SetPosition(int x, int y);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_GetKeyboardRect(out RECT rect);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_RegisterVisibilityNotify(IntPtr hwnd);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool T9_UnregisterVisibilityNotify(IntPtr hwnd);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern uint T9_GetVisibilityMessage();
    }
}
