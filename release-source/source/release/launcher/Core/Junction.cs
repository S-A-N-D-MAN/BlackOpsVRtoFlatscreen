// SPDX-License-Identifier: GPL-3.0-only
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace BlackOpsVR.Launcher;
internal static class Junction
{
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle file, uint code, byte[] input, int length, IntPtr output, int outputLength, out int returned, IntPtr overlapped);
    public static void Ensure(string link, string target)
    {
        target = Path.GetFullPath(target).TrimEnd(Path.DirectorySeparatorChar);
        if (!Directory.Exists(target) || target.StartsWith(@"\\")) throw new IOException("Select a game on a local Windows drive.");
        if (Directory.Exists(link))
        {
            var actual = new DirectoryInfo(link).ResolveLinkTarget(true);
            if (actual is null || !string.Equals(Path.GetFullPath(actual.FullName).TrimEnd('\\'), target, StringComparison.OrdinalIgnoreCase))
                throw new IOException("The existing VR asset link does not match the selected game. Your files were left unchanged.");
            return;
        }
        if (File.Exists(link)) throw new IOException("A file occupies the VR asset folder.");
        Directory.CreateDirectory(link);
        var substitute = Encoding.Unicode.GetBytes(@"\??\" + target);
        var print = Encoding.Unicode.GetBytes(target);
        var data = new byte[16 + substitute.Length + 2 + print.Length + 2];
        BitConverter.GetBytes(0xA0000003u).CopyTo(data, 0);
        BitConverter.GetBytes(checked((ushort)(data.Length-8))).CopyTo(data, 4);
        BitConverter.GetBytes(checked((ushort)substitute.Length)).CopyTo(data, 10);
        BitConverter.GetBytes(checked((ushort)(substitute.Length+2))).CopyTo(data, 12);
        BitConverter.GetBytes(checked((ushort)print.Length)).CopyTo(data, 14);
        substitute.CopyTo(data, 16); print.CopyTo(data, 18+substitute.Length);
        using var handle = CreateFile(link, 0x40000000, 7, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero);
        if (handle.IsInvalid || !DeviceIoControl(handle, 0x000900A4, data, data.Length, IntPtr.Zero, 0, out _, IntPtr.Zero))
        {
            var error = Marshal.GetLastWin32Error();
            // Delete only the empty directory we just created, never recursively.
            if (new DirectoryInfo(link).LinkTarget is null && !Directory.EnumerateFileSystemEntries(link).Any()) Directory.Delete(link, false);
            throw new IOException("Windows could not connect the VR setup to the game assets: " + new Win32Exception(error).Message);
        }
    }
}
