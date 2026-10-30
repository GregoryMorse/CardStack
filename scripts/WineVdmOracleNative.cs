using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace CardStack.Oracle {
public sealed class RunResult {
    public int ExitCode;
    public int ProcessId;
    public string Desktop;
    public string InputDesktopBefore;
    public string InputDesktopAfter;
    public bool TimedOut;
}
public static class Native {
    public const string DesktopPrefix = "CardStackOracle_";
    const uint AbortIfHung = 2, MessageTimeoutMs = 1500;
    const uint KillOnJobClose = 0x2000, CreateSuspended = 4, NoWindow = 0x08000000, UnicodeEnvironment = 0x400;
    [StructLayout(LayoutKind.Sequential)] struct SecurityAttributes { public int Length; public IntPtr Descriptor; [MarshalAs(UnmanagedType.Bool)] public bool Inherit; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct StartupInfo {
        public int Size; public string Reserved, Desktop, Title;
        public uint X,Y,XSize,YSize,XCount,YCount,Fill,Flags;
        public short Show, ReservedSize; public IntPtr ReservedBytes,Input,Output,Error;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr Process,Thread; public uint ProcessId,ThreadId; }
    [StructLayout(LayoutKind.Sequential)] struct JobLimits {
        public long ProcessTime,JobTime; public uint Flags; public UIntPtr MinWorking,MaxWorking;
        public uint ActiveProcesses; public UIntPtr Affinity; public uint Priority,Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)] struct IoCounters { public ulong ReadOps,WriteOps,OtherOps,ReadBytes,WriteBytes,OtherBytes; }
    [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits { public JobLimits Basic; public IoCounters Io; public UIntPtr ProcessMemory,JobMemory,PeakProcessMemory,PeakJobMemory; }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct Point { public int X,Y; }
    [StructLayout(LayoutKind.Sequential)] struct Message { public IntPtr Window; public uint Id; public UIntPtr WParam; public IntPtr LParam; public uint Time; public Point Point; public uint Private; }
    [StructLayout(LayoutKind.Sequential)] struct GuiInfo { public int Size,Flags; public IntPtr Active,Focus,Capture,MenuOwner,MoveSize,Caret; public Rect CaretRect; }
    public delegate bool Enumerate(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateDesktop(string name,IntPtr device,IntPtr mode,uint flags,uint access,IntPtr security);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll")] static extern IntPtr GetThreadDesktop(uint thread);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool GetUserObjectInformation(IntPtr handle,int index,StringBuilder text,int length,out int needed);
    [DllImport("user32.dll",SetLastError=true)] static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr security,string name);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int type,ref ExtendedLimits info,uint size);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
    [DllImport("kernel32.dll")] static extern bool TerminateJobObject(IntPtr job,uint exitCode);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr process,uint exitCode);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateProcess(string app,StringBuilder command,IntPtr processSecurity,IntPtr threadSecurity,bool inherit,uint flags,IntPtr environment,string directory,ref StartupInfo startup,out ProcessInfo process);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFile(string name,uint access,uint share,ref SecurityAttributes security,uint creation,uint attributes,IntPtr template);
    [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr process,out uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern bool IsWindowUnicode(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window,int id);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder text,int size);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="GetWindowLongPtrW")] static extern IntPtr WindowLong64(IntPtr window,int index);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="GetWindowLongW")] static extern int WindowLong32(IntPtr window,int index);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr window,ref Point point);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window,out Rect rect);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr window,Enumerate callback,IntPtr parameter);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr window,uint message,IntPtr wparam,IntPtr lparam,uint flags,uint timeout,out IntPtr result);
    [DllImport("user32.dll",CharSet=CharSet.Ansi,EntryPoint="SendMessageTimeoutA")] static extern IntPtr SendText(IntPtr window,uint message,IntPtr count,StringBuilder text,uint flags,uint timeout,out IntPtr result);
    [DllImport("user32.dll",CharSet=CharSet.Ansi,EntryPoint="SendMessageTimeoutA")] static extern IntPtr SetText(IntPtr window,uint message,IntPtr wparam,string text,uint flags,uint timeout,out IntPtr result);
    [DllImport("user32.dll",CharSet=CharSet.Ansi)] static extern bool PostMessage(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="PostMessageW")] static extern bool PostWideMessage(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageTimeoutW")] static extern IntPtr SendWideText(IntPtr window,uint message,IntPtr count,StringBuilder text,uint flags,uint timeout,out IntPtr result);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint key,uint type);
    [DllImport("user32.dll")] static extern bool PeekMessage(out Message message,IntPtr window,uint first,uint last,uint remove);
    // Both threads must be on the same private desktop before their queues can share test key state.
    [DllImport("user32.dll",SetLastError=true)] static extern bool AttachThreadInput(uint from,uint to,bool attach);
    [DllImport("user32.dll")] static extern bool SetKeyboardState(byte[] state);
    [DllImport("user32.dll")] static extern short GetKeyState(int key);
    [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr window);
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread,ref GuiInfo info);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
    [DllImport("user32.dll")] static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")] static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] static extern IntPtr GetSubMenu(IntPtr menu,int position);
    [DllImport("user32.dll")] static extern uint GetMenuItemID(IntPtr menu,int position);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetMenuString(IntPtr menu,uint item,StringBuilder text,int count,uint flags);
    [DllImport("user32.dll")] static extern uint GetMenuState(IntPtr menu,uint item,uint flags);

    static Exception Error(string operation) { return new Win32Exception(Marshal.GetLastWin32Error(),operation); }
    static string ObjectName(IntPtr handle) {
        if(handle==IntPtr.Zero) return "";
        int needed; var text=new StringBuilder(512);
        return GetUserObjectInformation(handle,2,text,text.Capacity*2,out needed) ? text.ToString() : "";
    }
    public static string DesktopName() { return ObjectName(GetThreadDesktop(GetCurrentThreadId())); }
    public static string InputDesktopName() {
        var handle=OpenInputDesktop(0,false,1);
        if(handle==IntPtr.Zero) return "<unavailable>";
        try{return ObjectName(handle);}finally{CloseDesktop(handle);}
    }
    public static void RequirePrivate() {
        string expected=Environment.GetEnvironmentVariable("CARDSTACK_ORACLE_PRIVATE_DESKTOP");
        if(String.IsNullOrEmpty(expected)||!expected.StartsWith(DesktopPrefix,StringComparison.Ordinal)
            ||DesktopName()!=expected||InputDesktopName()==expected)
            throw new InvalidOperationException("Oracle operations require a non-input private desktop; no foreground fallback is permitted.");
    }
    public static void RequireTarget(IntPtr window) {
        RequirePrivate();
        uint process; uint thread=GetWindowThreadProcessId(window,out process);
        if(!IsWindow(window)||ObjectName(GetThreadDesktop(thread))!=DesktopName())
            throw new InvalidOperationException("The target window is not on this run's private desktop.");
    }
    static string Quote(string value) {
        var result=new StringBuilder("\""); int slashes=0;
        foreach(char c in value) {
            if(c=='\\'){slashes++;continue;}
            if(c=='"'){result.Append('\\',slashes*2+1);result.Append(c);slashes=0;continue;}
            result.Append('\\',slashes);slashes=0;result.Append(c);
        }
        result.Append('\\',slashes*2);result.Append('"');return result.ToString();
    }
    public static RunResult Run(string executable,string[] arguments,string workingDirectory,string outputDirectory,int timeoutSeconds) {
        if(timeoutSeconds<1||timeoutSeconds>3600)throw new ArgumentOutOfRangeException("timeoutSeconds");
        Directory.CreateDirectory(outputDirectory);
        var result=new RunResult(); result.Desktop=DesktopPrefix+Guid.NewGuid().ToString("N");
        result.InputDesktopBefore=InputDesktopName();
        IntPtr desktop=IntPtr.Zero,job=IntPtr.Zero,stdout=IntPtr.Zero,stderr=IntPtr.Zero,stdin=IntPtr.Zero,environment=IntPtr.Zero;
        ProcessInfo process=new ProcessInfo();
        try {
            desktop=CreateDesktop(result.Desktop,IntPtr.Zero,IntPtr.Zero,0,0xC7,IntPtr.Zero);
            if(desktop==IntPtr.Zero)throw Error("Create private oracle desktop");
            job=CreateJobObject(IntPtr.Zero,null);if(job==IntPtr.Zero)throw Error("Create oracle job");
            var limits=new ExtendedLimits();limits.Basic.Flags=KillOnJobClose;
            if(!SetInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(limits)))throw Error("Configure oracle job");
            var security=new SecurityAttributes{Length=Marshal.SizeOf(typeof(SecurityAttributes)),Inherit=true};
            stdout=CreateFile(Path.Combine(outputDirectory,"worker.stdout.log"),0x40000000,3,ref security,2,0x80,IntPtr.Zero);
            stderr=CreateFile(Path.Combine(outputDirectory,"worker.stderr.log"),0x40000000,3,ref security,2,0x80,IntPtr.Zero);
            stdin=CreateFile("NUL",0x80000000,3,ref security,3,0x80,IntPtr.Zero);
            if(stdout.ToInt64()==-1||stderr.ToInt64()==-1||stdin.ToInt64()==-1)throw Error("Open oracle logs");
            var vars=new SortedDictionary<string,string>(StringComparer.OrdinalIgnoreCase);
            foreach(DictionaryEntry item in Environment.GetEnvironmentVariables())vars[(string)item.Key]=(string)item.Value;
            vars["CARDSTACK_ORACLE_PRIVATE_DESKTOP"]=result.Desktop;
            vars["CARDSTACK_TEST_ISOLATED_DESKTOP"]=result.Desktop;
            var block=new StringBuilder();foreach(var item in vars)block.Append(item.Key+"="+item.Value+'\0');block.Append('\0');
            environment=Marshal.StringToHGlobalUni(block.ToString());
            var startup=new StartupInfo{Size=Marshal.SizeOf(typeof(StartupInfo)),Desktop=result.Desktop,Flags=0x101,Show=1,Input=stdin,Output=stdout,Error=stderr};
            var command=new StringBuilder(Quote(executable));foreach(string argument in arguments)command.Append(" "+Quote(argument));
            if(!CreateProcess(executable,command,IntPtr.Zero,IntPtr.Zero,true,CreateSuspended|NoWindow|UnicodeEnvironment,environment,workingDirectory,ref startup,out process))throw Error("Create isolated oracle worker");
            result.ProcessId=(int)process.ProcessId;
            if(!AssignProcessToJobObject(job,process.Process)){TerminateProcess(process.Process,125);throw Error("Assign isolated worker to cleanup job");}
            if(ResumeThread(process.Thread)==UInt32.MaxValue)throw Error("Resume isolated worker");
            uint wait=WaitForSingleObject(process.Process,(uint)timeoutSeconds*1000);
            if(wait==0x102){result.TimedOut=true;result.ExitCode=124;TerminateJobObject(job,124);WaitForSingleObject(process.Process,5000);}
            else if(wait!=0)throw Error("Wait for isolated worker");
            else{uint code;if(!GetExitCodeProcess(process.Process,out code))throw Error("Read worker exit code");result.ExitCode=unchecked((int)code);}
            return result;
        } finally {
            if(job!=IntPtr.Zero){TerminateJobObject(job,0);CloseHandle(job);}
            if(process.Thread!=IntPtr.Zero)CloseHandle(process.Thread);
            if(process.Process!=IntPtr.Zero)CloseHandle(process.Process);
            foreach(var handle in new[]{stdout,stderr,stdin})if(handle!=IntPtr.Zero&&handle.ToInt64()!=-1)CloseHandle(handle);
            if(environment!=IntPtr.Zero)Marshal.FreeHGlobal(environment);
            if(desktop!=IntPtr.Zero)CloseDesktop(desktop);
            result.InputDesktopAfter=InputDesktopName();
        }
    }
    public static Process StartChild(string executable,string[] arguments,string directory) {
        RequirePrivate();
        var startup=new StartupInfo{Size=Marshal.SizeOf(typeof(StartupInfo)),Desktop=DesktopName(),Flags=1,Show=1};
        var command=new StringBuilder(Quote(executable));foreach(string argument in arguments)command.Append(" "+Quote(argument));
        ProcessInfo info;
        if(!CreateProcess(executable,command,IntPtr.Zero,IntPtr.Zero,false,NoWindow,IntPtr.Zero,directory,ref startup,out info))throw Error("Start private-desktop child");
        try{var child=Process.GetProcessById((int)info.ProcessId);var retainedHandle=child.Handle;return child;}finally{CloseHandle(info.Thread);CloseHandle(info.Process);}
    }
    public static RunResult WaitChild(Process child,int milliseconds) {
        RequirePrivate();var result=new RunResult{ProcessId=child.Id,Desktop=DesktopName()};
        result.TimedOut=!child.WaitForExit(milliseconds);
        if(result.TimedOut){child.Kill();child.WaitForExit();}
        result.ExitCode=result.TimedOut?124:child.ExitCode;return result;
    }
    public static IntPtr Send(IntPtr window,uint message,IntPtr wparam,IntPtr lparam) {
        RequireTarget(window); IntPtr result;
        if(SendMessageTimeout(window,message,wparam,lparam,AbortIfHung,MessageTimeoutMs,out result)==IntPtr.Zero)throw Error("Timed window message");
        return result;
    }
    public static void Post(IntPtr window,uint message,IntPtr wparam,IntPtr lparam) {
        RequireTarget(window);if(!PostMessage(window,message,wparam,lparam))throw Error("Post window message");
    }
    public static string ReadText(IntPtr window) {
        RequireTarget(window);var text=new StringBuilder(16384);IntPtr result;
        IntPtr sent=IsWindowUnicode(window)?SendWideText(window,13,(IntPtr)text.Capacity,text,AbortIfHung,MessageTimeoutMs,out result):SendText(window,13,(IntPtr)text.Capacity,text,AbortIfHung,MessageTimeoutMs,out result);
        if(sent==IntPtr.Zero)throw Error("Read window text");
        return text.ToString();
    }
    public static void WriteText(IntPtr window,string text) {
        RequireTarget(window);IntPtr result;
        if(SetText(window,12,IntPtr.Zero,text,AbortIfHung,MessageTimeoutMs,out result)==IntPtr.Zero)throw Error("Set window text");
        if(ReadText(window)!=text)throw new InvalidOperationException("WM_SETTEXT readback differs from requested text.");
    }
    public static void Click(IntPtr window,int x,int y,bool twice) {
        RequireTarget(window);Rect rect;if(!GetClientRect(window,out rect))throw Error("Read client rectangle");
        if(x<0)x=Math.Max(0,(rect.Right-rect.Left)/2);if(y<0)y=Math.Max(0,(rect.Bottom-rect.Top)/2);
        if(x<0||y<0||x>=rect.Right||y>=rect.Bottom||x>32767||y>32767)throw new ArgumentOutOfRangeException("Mouse client coordinates");
        var point=(IntPtr)((y<<16)|(x&65535));
        Post(window,0x201,(IntPtr)1,point);Post(window,0x202,IntPtr.Zero,point);
        if(twice){Post(window,0x203,(IntPtr)1,point);Post(window,0x202,IntPtr.Zero,point);}
    }
    public static void TypeText(IntPtr window,string text) {
        RequireTarget(window);
        const uint EditSetSelection = 0xB1, Win16EditSetSelection = 0x401, Character = 0x102;
        bool win16=ClassName(window).StartsWith("WIN16",StringComparison.OrdinalIgnoreCase);
        if(win16) Send(window,Win16EditSetSelection,IntPtr.Zero,(IntPtr)(-65536));
        else Send(window,EditSetSelection,IntPtr.Zero,(IntPtr)(-1));
        // A typed line break is one Enter character, not separate CR and LF keystrokes.
        string input=text.Replace("\r\n","\r").Replace('\n','\r');
        if(input.Length==0){Post(window,Character,(IntPtr)8,(IntPtr)1);return;}
        if(IsWindowUnicode(window)){
            foreach(char value in input)if(!PostWideMessage(window,Character,(IntPtr)value,(IntPtr)1))throw Error("Post Unicode character");
        }else{
            var encoding=Encoding.GetEncoding(1252,EncoderFallback.ExceptionFallback,DecoderFallback.ExceptionFallback);
            foreach(byte value in encoding.GetBytes(input))Post(window,Character,(IntPtr)value,(IntPtr)1);
        }
    }
    public static void FocusPrivateWindow(IntPtr window) {
        RequireTarget(window);uint process;uint target=GetWindowThreadProcessId(window,out process),current=GetCurrentThreadId();
        Message message;PeekMessage(out message,IntPtr.Zero,0,0,0);bool attached=false;
        try{
            if(target!=current){if(!AttachThreadInput(current,target,true))throw Error("Attach private focus queues");attached=true;}
            SetFocus(window);
            if(FocusWindow(window)!=window)throw new InvalidOperationException("Private logical focus did not reach the requested control.");
        }finally{if(attached)AttachThreadInput(current,target,false);}
    }
    public static IntPtr FocusWindow(IntPtr window) {
        RequireTarget(window);uint process;uint thread=GetWindowThreadProcessId(window,out process);
        var info=new GuiInfo{Size=Marshal.SizeOf(typeof(GuiInfo))};
        if(!GetGUIThreadInfo(thread,ref info))throw Error("Read target-thread focus");
        return info.Focus;
    }
    public sealed class KeyLease : IDisposable {
        readonly IntPtr window;readonly uint target,current;readonly byte[] state=new byte[256];readonly List<int> modifiers=new List<int>();bool attached;
        internal KeyLease(IntPtr window,string[] names) {
            RequireTarget(window);this.window=window;uint pid;target=GetWindowThreadProcessId(window,out pid);current=GetCurrentThreadId();
            Message message;PeekMessage(out message,IntPtr.Zero,0,0,0);
            if(target!=current){if(!AttachThreadInput(current,target,true))throw Error("Attach private keyboard queues");attached=true;}
            try {
                foreach(string name in names) {
                    if(String.IsNullOrWhiteSpace(name))continue;
                    int key=name.Equals("Control",StringComparison.OrdinalIgnoreCase)?17:name.Equals("Shift",StringComparison.OrdinalIgnoreCase)?16:name.Equals("Alt",StringComparison.OrdinalIgnoreCase)?18:0;
                    if(key==0)throw new ArgumentException("Unknown test modifier: "+name);
                    modifiers.Add(key);state[key]=0x80;
                }
                if(!SetKeyboardState(state))throw Error("Set private keyboard state");
                foreach(int key in modifiers)PostKey(key,false);
            }catch{Dispose();throw;}
        }
        void PostKey(int key,bool up) {
            if(up&&!IsWindow(window))return;
            long bits=1|((long)MapVirtualKey((uint)key,0)<<16);
            if(key>=33&&key<=46)bits|=1L<<24;
            if(modifiers.Contains(18))bits|=1L<<29;
            if(up)bits|=3L<<30;
            Post(window,(uint)(modifiers.Contains(18)?(up?0x105:0x104):(up?0x101:0x100)),(IntPtr)key,(IntPtr)bits);
        }
        public void Down(int key){state[key]=0x80;SetKeyboardState(state);PostKey(key,false);}
        public void Up(int key){state[key]=0;SetKeyboardState(state);PostKey(key,true);}
        public void Dispose(){
            try{foreach(int key in modifiers){state[key]=0;SetKeyboardState(state);PostKey(key,true);}SetKeyboardState(new byte[256]);}
            finally{SetKeyboardState(new byte[256]);if(attached){AttachThreadInput(current,target,false);attached=false;}}
        }
    }
    public static KeyLease BeginKeys(IntPtr window,string[] modifiers){return new KeyLease(window,modifiers);}
    public static string ClassName(IntPtr window){var text=new StringBuilder(256);GetClassName(window,text,text.Capacity);return text.ToString();}
    public static long Style(IntPtr window){return IntPtr.Size==8?WindowLong64(window,-16).ToInt64():WindowLong32(window,-16);}
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct LogFont {
        public int Height,Width,Escapement,Orientation,Weight;
        public byte Italic,Underline,StrikeOut,CharacterSet,OutputPrecision,ClipPrecision,Quality,PitchAndFamily;
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)] public string Face;
    }
    [DllImport("gdi32.dll",CharSet=CharSet.Unicode,EntryPoint="GetObjectW")] static extern int FontObject(IntPtr font,int size,out LogFont value);
    static int LowInt(IntPtr value) { return unchecked((int)value.ToInt64()); }
    [StructLayout(LayoutKind.Sequential)] struct TextSize { public int Width,Height; }
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr window,IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc,IntPtr value);
    [DllImport("gdi32.dll",CharSet=CharSet.Unicode,EntryPoint="CreateFontIndirectW")] static extern IntPtr CreateLocalFont(ref LogFont description);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr value);
    [DllImport("gdi32.dll",CharSet=CharSet.Unicode,EntryPoint="GetTextExtentPoint32W")] static extern bool TextExtent(IntPtr dc,string text,int length,out TextSize size);
    public static object WidestItem(IntPtr window) {
        RequireTarget(window);string type=ClassName(window);
        bool combo=type.Equals("ComboBox",StringComparison.OrdinalIgnoreCase)||type.StartsWith("WindowsForms10.COMBOBOX.",StringComparison.OrdinalIgnoreCase);
        bool list=type.Equals("ListBox",StringComparison.OrdinalIgnoreCase)||type.StartsWith("WindowsForms10.LISTBOX.",StringComparison.OrdinalIgnoreCase);
        if(!combo&&!list)throw new ArgumentException("Text measurement requires a combo or list box.");
        int count=LowInt(Send(window,combo?0x146u:0x18Bu,IntPtr.Zero,IntPtr.Zero));
        if(count<1||count>1024)throw new InvalidOperationException("Unsupported item count for text measurement.");
        var dc=GetDC(window);if(dc==IntPtr.Zero)throw Error("Acquire control text DC");
        var font=Send(window,0x31,IntPtr.Zero,IntPtr.Zero);IntPtr previous=IntPtr.Zero,localFont=IntPtr.Zero;
        int best=-1,width=-1,height=0;string text="";
        try {
            if(font!=IntPtr.Zero){
                // A foreign process's HFONT is inspectable but cannot be selected into our DC.
                LogFont description;
                if(FontObject(font,Marshal.SizeOf(typeof(LogFont)),out description)<=0)throw Error("Read control font description");
                localFont=CreateLocalFont(ref description);if(localFont==IntPtr.Zero)throw Error("Clone control font");
                previous=SelectObject(dc,localFont);if(previous==IntPtr.Zero||previous==(IntPtr)(-1))throw Error("Select cloned control font");
            }
            for(int index=0;index<count;index++) {
                int length=LowInt(Send(window,combo?0x149u:0x18Au,(IntPtr)index,IntPtr.Zero));
                if(length<0||length>32767)throw new InvalidOperationException("Item text length is unavailable or unsupported.");
                var buffer=new StringBuilder(length+1);IntPtr result;
                IntPtr sent=IsWindowUnicode(window)
                    ?SendWideText(window,combo?0x148u:0x189u,(IntPtr)index,buffer,AbortIfHung,MessageTimeoutMs,out result)
                    :SendText(window,combo?0x148u:0x189u,(IntPtr)index,buffer,AbortIfHung,MessageTimeoutMs,out result);
                if(sent==IntPtr.Zero||LowInt(result)<0)throw Error("Read control item text");
                string candidate=buffer.ToString();TextSize extent;
                if(!TextExtent(dc,candidate,candidate.Length,out extent))throw Error("Measure control item text");
                if(extent.Width>width){best=index;width=extent.Width;height=extent.Height;text=candidate;}
            }
        }finally{if(previous!=IntPtr.Zero&&previous!=(IntPtr)(-1))SelectObject(dc,previous);if(localFont!=IntPtr.Zero)DeleteObject(localFont);ReleaseDC(window,dc);}
        return new Dictionary<string,object>{{"index",best},{"text",text},{"widthPixels",width},{"heightPixels",height},{"method","GetTextExtentPoint32W"},{"fontSource",font==IntPtr.Zero?"DC default":"WM_GETFONT LOGFONT clone"}};
    }
    public static object Describe(IntPtr window) {
        RequireTarget(window);Rect rect;GetWindowRect(window,out rect);string type=ClassName(window);long style=Style(window);
        bool password=type.Equals("Edit",StringComparison.OrdinalIgnoreCase)&&(style&0x20)!=0;
        var value=new Dictionary<string,object>{{"handle",window.ToInt64()},{"id",GetDlgCtrlID(window)},{"class",type},{"text",password?"<redacted>":ReadText(window)},
            {"visible",IsWindowVisible(window)},{"enabled",IsWindowEnabled(window)},{"style",String.Format("0x{0:X8}",unchecked((uint)style))},
            {"rect",new[]{rect.Left,rect.Top,rect.Right-rect.Left,rect.Bottom-rect.Top}}};
        if(type.Equals("Button",StringComparison.OrdinalIgnoreCase))value["checked"]=LowInt(Send(window,0xF0,IntPtr.Zero,IntPtr.Zero));
        if(type.Equals("ComboBox",StringComparison.OrdinalIgnoreCase))value["selectedIndex"]=LowInt(Send(window,0x147,IntPtr.Zero,IntPtr.Zero));
        if(type.Equals("ListBox",StringComparison.OrdinalIgnoreCase))value["selectedIndex"]=LowInt(Send(window,0x188,IntPtr.Zero,IntPtr.Zero));
        LogFont font;var fontHandle=Send(window,0x31,IntPtr.Zero,IntPtr.Zero);
        if(fontHandle!=IntPtr.Zero&&FontObject(fontHandle,Marshal.SizeOf(typeof(LogFont)),out font)>0)
            value["font"]=new Dictionary<string,object>{{"family",font.Face},{"heightPixels",font.Height},{"widthPixels",font.Width},{"weight",font.Weight},{"italic",font.Italic!=0},{"characterSet",font.CharacterSet}};
        else value["fontStatus"]="not exposed by this window";
        Rect client;GetClientRect(window,out client);var origin=new Point();ClientToScreen(window,ref origin);
        value["clientOrigin"]=new[]{origin.X,origin.Y};value["clientSize"]=new[]{client.Right,client.Bottom};value["dpi"]=GetDpiForWindow(window);
        bool combo=type.Equals("ComboBox",StringComparison.OrdinalIgnoreCase),list=type.Equals("ListBox",StringComparison.OrdinalIgnoreCase);
        if(combo||list){
            int count=LowInt(Send(window,combo?0x146u:0x18Bu,IntPtr.Zero,IntPtr.Zero));var items=new List<string>();
            for(int i=0;i<count&&i<1024;i++){
                int length=LowInt(Send(window,combo?0x149u:0x18Au,(IntPtr)i,IntPtr.Zero));
                if(length<0||length>1024){items.Add("<unavailable>");continue;}
                var text=new StringBuilder(2048);IntPtr result;
                if(SendText(window,combo?0x148u:0x189u,(IntPtr)i,text,AbortIfHung,MessageTimeoutMs,out result)==IntPtr.Zero)throw Error("Read list item");
                items.Add(text.ToString());
            }
            value["items"]=items.ToArray();
        }
        return value;
    }
    public static object[] Controls(IntPtr window) {
        RequireTarget(window);var result=new List<object>();
        EnumChildWindows(window,delegate(IntPtr child,IntPtr p){if(result.Count>=4096)return false;if(IsWindow(child))result.Add(Describe(child));return true;},IntPtr.Zero);
        return result.ToArray();
    }
    static void ReadMenu(IntPtr menu,string path,List<object> output,int depth) {
        if(depth>8||output.Count>512)return;
        for(int i=0;i<GetMenuItemCount(menu)&&output.Count<512;i++){
            var text=new StringBuilder(512);GetMenuString(menu,(uint)i,text,text.Capacity,0x400);uint state=GetMenuState(menu,(uint)i,0x400);
            string key=path+"/"+text.ToString();output.Add(new Dictionary<string,object>{{"id",GetMenuItemID(menu,i)},{"path",key},{"checked",(state&8)!=0},{"enabled",(state&3)==0}});
            var child=GetSubMenu(menu,i);if(child!=IntPtr.Zero)ReadMenu(child,key,output,depth+1);
        }
    }
    public static object[] Menus(IntPtr window) {
        RequireTarget(window);var output=new List<object>();var menu=GetMenu(window);if(menu==IntPtr.Zero)return output.ToArray();
        try {
            Send(window,0x211,IntPtr.Zero,IntPtr.Zero);
            Send(window,0x116,menu,IntPtr.Zero);
            for(int i=0;i<GetMenuItemCount(menu);i++){var child=GetSubMenu(menu,i);if(child!=IntPtr.Zero)Send(window,0x117,child,(IntPtr)i);}
            ReadMenu(menu,"",output,0);return output.ToArray();
        } finally {
            Send(window,0x11F,(IntPtr)unchecked((int)0xFFFF0000),IntPtr.Zero);
            Send(window,0x212,IntPtr.Zero,IntPtr.Zero);
        }
    }
    public static object Capture(IntPtr window,string path) {
        RequireTarget(window);Rect rect;if(!GetWindowRect(window,out rect))throw Error("Read capture bounds");
        int width=rect.Right-rect.Left,height=rect.Bottom-rect.Top;
        if(width<1||height<1||width>8192||height>8192||(long)width*height>16000000)throw new InvalidOperationException("Unsupported capture dimensions.");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
        using(var bitmap=new Bitmap(width,height,PixelFormat.Format24bppRgb)){
            using(var graphics=Graphics.FromImage(bitmap)){graphics.Clear(Color.Magenta);var dc=graphics.GetHdc();try{if(!PrintWindow(window,dc,0))throw Error("PrintWindow capture");}finally{graphics.ReleaseHdc(dc);}}
            var colors=new HashSet<int>();for(int y=0;y<height;y+=Math.Max(1,height/32))for(int x=0;x<width;x+=Math.Max(1,width/32))colors.Add(bitmap.GetPixel(x,y).ToArgb());
            bitmap.Save(path,ImageFormat.Png);
            if(colors.Count<3)throw new InvalidOperationException("Capture is blank or uniform; diagnostic PNG saved. No desktop screen-copy fallback is permitted.");
            return new Dictionary<string,object>{{"width",width},{"height",height},{"sampleColors",colors.Count},{"method","PrintWindow"},{"visualReviewRequired",true}};
        }
    }
    public static object SelfTest(string output) {
        RequirePrivate();Directory.CreateDirectory(output);var ready=new ManualResetEvent(false);string keyTrace="";Exception failure=null;IntPtr form=IntPtr.Zero,edit=IntPtr.Zero,button=IntPtr.Zero,label=IntPtr.Zero,multiline=IntPtr.Zero,comboBox=IntPtr.Zero,listBox=IntPtr.Zero;
        var thread=new Thread(delegate(){try{
            var f=new Form{Text="Oracle message-driver self-test",Width=420,Height=350};
            var e=new TextBox{Left=20,Top=20,Width=330};e.KeyDown+=delegate(object sender,KeyEventArgs args){keyTrace+=args.KeyData+" ctrl="+GetKeyState(17)+";";if(args.Control&&args.KeyCode==Keys.A){e.SelectAll();args.SuppressKeyPress=true;}};var b=new Button{Left=20,Top=65,Width=100,Text="Test"};
            var l=new Label{Left=140,Top=70,Width=180,Text="Not clicked"};b.Click+=delegate{l.Text="Clicked";};
            var m=new TextBox{Left=20,Top=100,Width=330,Height=80,Multiline=true};
            var c=new ComboBox{Left=20,Top=195,Width=160,DropDownStyle=ComboBoxStyle.DropDownList,Font=new Font("Arial",12)};
            var list=new ListBox{Left=195,Top=195,Width=160,Height=100,Font=new Font("Arial",12)};
            c.Items.AddRange(new object[]{"iiiiiiii","WWWW"});list.Items.AddRange(new object[]{"iiiiiiii","WWWW"});
            f.Controls.AddRange(new Control[]{e,b,l,m,c,list});f.Shown+=delegate{form=f.Handle;edit=e.Handle;button=b.Handle;label=l.Handle;multiline=m.Handle;comboBox=c.Handle;listBox=list.Handle;e.Focus();ready.Set();};Application.Run(f);
        }catch(Exception ex){failure=ex;ready.Set();}});
        thread.SetApartmentState(ApartmentState.STA);thread.IsBackground=true;thread.Start();
        if(!ready.WaitOne(5000)||failure!=null)throw new InvalidOperationException("Self-test UI did not initialize.",failure);
        WriteText(edit,"Alpha Beta Gamma");Send(edit,0xB1,(IntPtr)6,(IntPtr)10);Post(edit,0x102,(IntPtr)'Z',(IntPtr)1);
        var deadline=Stopwatch.StartNew();while(ReadText(edit)!="Alpha Z Gamma"&&deadline.ElapsedMilliseconds<2000)Thread.Sleep(10);
        if(ReadText(edit)!="Alpha Z Gamma")throw new InvalidOperationException("Character messages did not preserve surrounding text.");
        Click(button,-1,-1,false);deadline.Restart();while(ReadText(label)!="Clicked"&&deadline.ElapsedMilliseconds<2000)Thread.Sleep(10);
        if(ReadText(label)!="Clicked")throw new InvalidOperationException("Mouse messages did not activate the button.");
        Click(edit,5,5,false);Thread.Sleep(50);
        using(var keys=BeginKeys(edit,new[]{"Control"})){
            keys.Down(65);deadline.Restart();
            while(LowInt(Send(edit,0xB0,IntPtr.Zero,IntPtr.Zero))!=(ReadText(edit).Length<<16)&&deadline.ElapsedMilliseconds<2000)Thread.Sleep(10);
            keys.Up(65);
            if(LowInt(Send(edit,0xB0,IntPtr.Zero,IntPtr.Zero))!=(ReadText(edit).Length<<16)){ Capture(form,Path.Combine(output,"driver-modifier-failure.png")); throw new InvalidOperationException("Private-queue Ctrl+A did not select all text. Focus="+FocusWindow(edit)+" edit="+edit+" trace="+keyTrace+" controllerCtrl="+GetKeyState(17)); }
        }
        string multilineExpected="Caf\u00e9\r\nSecond \u2122";
        TypeText(multiline,multilineExpected);deadline.Restart();
        while(ReadText(multiline)!=multilineExpected&&deadline.ElapsedMilliseconds<2000)Thread.Sleep(10);
        if(ReadText(multiline)!=multilineExpected)throw new InvalidOperationException("Multiline character input changed Unicode text or line breaks: "+ReadText(multiline).Replace("\r","<CR>").Replace("\n","<LF>"));
        TypeText(multiline,"");deadline.Restart();
        while(ReadText(multiline)!=""&&deadline.ElapsedMilliseconds<2000)Thread.Sleep(10);
        if(ReadText(multiline)!="")throw new InvalidOperationException("Empty typed text failed to delete the selected value.");
        TypeText(multiline,multilineExpected);deadline.Restart();
        while(ReadText(multiline)!=multilineExpected&&deadline.ElapsedMilliseconds<2000)Thread.Sleep(10);
        if(ReadText(multiline)!=multilineExpected)throw new InvalidOperationException("Text could not be reentered after clearing.");
        var measuredCombo=(Dictionary<string,object>)WidestItem(comboBox);
        var measuredList=(Dictionary<string,object>)WidestItem(listBox);
        if((int)measuredCombo["index"]!=1||(int)measuredList["index"]!=1)
            throw new InvalidOperationException("Widest-item selection used character count instead of the control's font metrics.");
        var image=Capture(form,Path.Combine(output,"driver-self-test.png"));Post(form,0x10,IntPtr.Zero,IntPtr.Zero);thread.Join(2000);
        return new Dictionary<string,object>{{"desktop",DesktopName()},{"inputDesktop",InputDesktopName()},{"mouse",true},{"characterSelection",true},{"controlModifier",true},{"multilineUnicode",true},{"emptyTextClearsSelection",true},{"widestComboItem",measuredCombo},{"widestListItem",measuredList},{"image",image}};
    }
}
}
