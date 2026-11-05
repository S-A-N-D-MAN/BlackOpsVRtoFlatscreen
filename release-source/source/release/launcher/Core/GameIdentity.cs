// SPDX-License-Identifier: GPL-3.0-only
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace BlackOpsVR.Launcher;

public sealed record GameIdentityResult(bool Accepted, string Sha256, long Bytes, string Reason, string Signer);

public static class GameIdentity
{
    public const long ReferenceBytes = 8_101_944;
    public const long ImageFileBytes = 8_096_256;
    public const long MaximumCertificateBytes = 131_072;
    public const string ReferenceSha256 = "db00603ddea3ee201c5870c7bb27e27fc569528839e92a3f78689c256dd73ff3";
    static readonly Guid VerifyV2 = new("00AAC56B-CD44-11D0-8CC2-00C04FC295EE");

    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct WinTrustFileInfo
    {
        public uint Size;
        [MarshalAs(UnmanagedType.LPWStr)] public string Path;
        public IntPtr File;
        public IntPtr KnownSubject;
    }

    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct WinTrustData
    {
        public uint Size;
        public IntPtr PolicyCallbackData;
        public IntPtr SipClientData;
        public uint UiChoice;
        public uint RevocationChecks;
        public uint UnionChoice;
        public IntPtr FileInfo;
        public uint StateAction;
        public IntPtr StateData;
        public IntPtr UrlReference;
        public uint ProviderFlags;
        public uint UiContext;
    }

    [DllImport("wintrust.dll", ExactSpelling=true, PreserveSig=true)]
    static extern int WinVerifyTrust(IntPtr window, [In] ref Guid action, ref WinTrustData data);

    static bool PeFamily(Stream stream, out string reason)
    {
        reason = "BlackOps.exe PE layout does not match the supported Steam single-player build";
        if (stream.Length < ImageFileBytes+8 || stream.Length > ImageFileBytes+MaximumCertificateBytes) { reason = $"unexpected executable size {stream.Length} bytes"; return false; }
        using var reader = new BinaryReader(stream, System.Text.Encoding.UTF8, leaveOpen:true);
        if (reader.ReadUInt16() != 0x5a4d) { reason = "missing Windows MZ header"; return false; }
        stream.Position = 0x3c;
        var pe = reader.ReadInt32();
        if (pe < 0x40 || pe > stream.Length-248) { reason = "invalid Windows PE header offset"; return false; }
        stream.Position = pe;
        if (reader.ReadUInt32() != 0x4550 || reader.ReadUInt16() != 0x14c || reader.ReadUInt16() != 7 ||
            reader.ReadUInt32() != 1_314_918_417u) return false;
        stream.Position = pe+20;
        if (reader.ReadUInt16() != 224 || reader.ReadUInt16() != 291) return false;
        var optional = pe+24;
        stream.Position = optional;
        if (reader.ReadUInt16() != 0x10b) return false;
        stream.Position = optional+16;
        if (reader.ReadUInt32() != 0x56da57) return false;
        stream.Position = optional+28;
        if (reader.ReadUInt32() != 0x400000) return false;
        stream.Position = optional+56;
        if (reader.ReadUInt32() != 69_693_440u) return false;
        stream.Position = optional+70;
        if (reader.ReadUInt16() != 32_768) return false;
        stream.Position = optional+128;
        var certificateOffset=reader.ReadUInt32();var certificateSize=reader.ReadUInt32();
        if(certificateOffset!=ImageFileBytes||certificateSize<8||certificateOffset+certificateSize!=stream.Length)
        { reason="signed file bounds do not match the Black Ops Steam image";return false; }
        var expected = new (string Name,uint Va,uint VirtualSize,uint RawSize,uint RawPointer,uint Characteristics)[] {
            (".text",0x1000,0x5a1a10,5_905_408,1_024,0x60000020),
            (".rdata",0x5a3000,0x1c86ba,1_869_824,5_906_432,0x40000040),
            (".data",0x76c000,0x3af36c4,238_592,7_776_256,0xc0000040),
            (".tls",0x4260000,0x6d,512,8_014_848,0xc0000040),
            (".rodata",0x4261000,0x770,2_048,8_015_360,0x40000040),
            (".version",0x4262000,0x4,512,8_017_408,0x40000040),
            (".rsrc",0x4263000,0x130c4,78_336,8_017_920,0x40000040)
        };
        stream.Position = optional+224;
        foreach (var section in expected)
        {
            var name = System.Text.Encoding.ASCII.GetString(reader.ReadBytes(8)).TrimEnd('\0');
            var virtualSize=reader.ReadUInt32();
            var va = reader.ReadUInt32();
            var rawSize = reader.ReadUInt32();
            var rawPointer=reader.ReadUInt32();
            stream.Position += 12;
            var characteristics=reader.ReadUInt32();
            if (name != section.Name || va != section.Va || virtualSize!=section.VirtualSize||rawSize != section.RawSize||rawPointer!=section.RawPointer||characteristics!=section.Characteristics) return false;
        }
        reason = "supported Black Ops single-player PE layout";
        return true;
    }

    static bool TrustedValveSignature(string path, out string signer, out string reason)
    {
        signer = "";
        reason = "executable Authenticode signature is invalid";
        var file = new WinTrustFileInfo { Size=(uint)Marshal.SizeOf<WinTrustFileInfo>(), Path=path };
        var filePointer = Marshal.AllocHGlobal(Marshal.SizeOf<WinTrustFileInfo>());
        try
        {
            Marshal.StructureToPtr(file,filePointer,false);
            var trust = new WinTrustData {
                Size=(uint)Marshal.SizeOf<WinTrustData>(), UiChoice=2, RevocationChecks=0,
                UnionChoice=1, FileInfo=filePointer, StateAction=1, ProviderFlags=0x1000
            };
            var action = VerifyV2;
            var status = WinVerifyTrust(new IntPtr(-1),ref action,ref trust);
            trust.StateAction=2;
            WinVerifyTrust(new IntPtr(-1),ref action,ref trust);
            if (status != 0) return false;
            using var certificate = new X509Certificate2(X509Certificate.CreateFromSignedFile(path));
            signer = certificate.GetNameInfo(X509NameType.SimpleName,false);
            if (signer is not ("Valve Corp." or "Valve")) { reason = "executable signer is not Valve"; return false; }
            reason = "valid Valve Authenticode signature";
            return true;
        }
        catch (CryptographicException)
        {
            return false;
        }
        finally
        {
            Marshal.FreeHGlobal(filePointer);
        }
    }

    public static GameIdentityResult Inspect(string path)
    {
        if (!File.Exists(path)) return new(false,"",0,"BlackOps.exe was not found","");
        string sha;
        long bytes;
        string layoutReason;
        try
        {
            using var stream = File.Open(path,FileMode.Open,FileAccess.Read,FileShare.Read|FileShare.Delete);
            bytes=stream.Length;
            if (!PeFamily(stream,out layoutReason)) return new(false,"",bytes,layoutReason,"");
            stream.Position=0;
            sha=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(stream)).ToLowerInvariant();
        }
        catch (IOException e)
        {
            return new(false,"",0,"could not read executable: "+e.Message,"");
        }
        if (sha==ReferenceSha256) return new(true,sha,bytes,"reference Steam executable accepted","Valve Corp.");
        if (!TrustedValveSignature(path,out var signer,out var trustReason)) return new(false,sha,bytes,trustReason,signer);
        return new(true,sha,bytes,"Valve-signed Steam executable family accepted",signer);
    }
}
