using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;
using SYSKIND = System.Runtime.InteropServices.ComTypes.SYSKIND;
using TYPEATTR = System.Runtime.InteropServices.ComTypes.TYPEATTR;
using TYPEFLAGS = System.Runtime.InteropServices.ComTypes.TYPEFLAGS;
using TYPEKIND = System.Runtime.InteropServices.ComTypes.TYPEKIND;
using TYPELIBATTR = System.Runtime.InteropServices.ComTypes.TYPELIBATTR;

public sealed class BwtlCreoComTypeLibrary : IDisposable
{
    private static readonly Guid VendorId = new Guid("176453f2-6934-4304-8c9d-126d98c1700e");
    private readonly ITypeLib library;
    private readonly TYPELIBATTR attributes;
    private readonly string vendorPath;
    private readonly string[] registryKeys;
    private bool registered;

    [DllImport("oleaut32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    private static extern void LoadTypeLibEx(string path, int registration, out ITypeLib typeLibrary);

    [DllImport("oleaut32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    private static extern void QueryPathOfRegTypeLib(ref Guid id, short major, short minor, int lcid,
        [MarshalAs(UnmanagedType.BStr)] out string path);

    [DllImport("oleaut32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    private static extern void RegisterTypeLibForUser(ITypeLib typeLibrary, string path, string helpDirectory);

    [DllImport("oleaut32.dll", PreserveSig = false)]
    private static extern void UnRegisterTypeLibForUser(ref Guid id, short major, short minor, int lcid, SYSKIND systemKind);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetNamedPipeClientSessionId(SafePipeHandle pipe, out uint session);

    public static bool ClientIsInSession(SafePipeHandle pipe, uint expected)
    {
        uint actual;
        return GetNamedPipeClientSessionId(pipe, out actual) && actual == expected;
    }

    public BwtlCreoComTypeLibrary(string path)
    {
        vendorPath = Path.GetFullPath(path);
        LoadTypeLibEx(vendorPath, 2, out library); // REGKIND_NONE
        try
        {
            IntPtr raw;
            library.GetLibAttr(out raw);
            try { attributes = (TYPELIBATTR)Marshal.PtrToStructure(raw, typeof(TYPELIBATTR)); }
            finally { library.ReleaseTLibAttr(raw); }

            if (attributes.guid != VendorId || attributes.wMajorVerNum != 1 ||
                attributes.wMinorVerNum != 0 || attributes.lcid != 0)
                throw new InvalidOperationException("Unexpected Creo VB API type library identity.");

            var keys = new List<string> { @"TypeLib\" + VendorId.ToString("B") };
            int count = library.GetTypeInfoCount();
            for (int i = 0; i < count; ++i)
            {
                TYPEKIND kind;
                library.GetTypeInfoType(i, out kind);
                if (kind != TYPEKIND.TKIND_INTERFACE && kind != TYPEKIND.TKIND_DISPATCH) continue;

                ITypeInfo entry;
                library.GetTypeInfo(i, out entry);
                try
                {
                    entry.GetTypeAttr(out raw);
                    try
                    {
                        var info = (TYPEATTR)Marshal.PtrToStructure(raw, typeof(TYPEATTR));
                        if (kind == TYPEKIND.TKIND_DISPATCH ||
                            (info.wTypeFlags & (TYPEFLAGS.TYPEFLAG_FOLEAUTOMATION | TYPEFLAGS.TYPEFLAG_FDUAL)) != 0)
                            keys.Add(@"Interface\" + info.guid.ToString("B"));
                    }
                    finally { entry.ReleaseTypeAttr(raw); }
                }
                finally { Marshal.ReleaseComObject(entry); }
            }
            registryKeys = keys.ToArray();
        }
        catch
        {
            Marshal.ReleaseComObject(library);
            throw;
        }
    }

    public int CheckedRegistrationKeys { get { return registryKeys.Length; } }

    private string RegisteredPath()
    {
        var id = attributes.guid;
        try
        {
            string path;
            QueryPathOfRegTypeLib(ref id, attributes.wMajorVerNum, attributes.wMinorVerNum, attributes.lcid, out path);
            return path;
        }
        catch (COMException error)
        {
            if (error.ErrorCode == unchecked((int)0x8002801D)) return null; // TYPE_E_LIBNOTREGISTERED
            throw;
        }
    }

    private void RequireMissingRegistration()
    {
        foreach (RegistryView view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
        {
            foreach (RegistryHive hive in new[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine })
            {
                using (RegistryKey baseKey = RegistryKey.OpenBaseKey(hive, view))
                {
                    foreach (string path in registryKeys)
                    {
                        using (RegistryKey existing = baseKey.OpenSubKey(@"Software\Classes\" + path))
                        {
                            if (existing != null)
                                throw new InvalidOperationException("A conflicting Creo VB API type-library or interface registration already exists.");
                        }
                    }
                }
            }
        }
    }

    public bool Acquire()
    {
        string current = RegisteredPath();
        if (current != null)
        {
            if (!String.Equals(Path.GetFullPath(current), vendorPath, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("The registered VB API points to a different Creo installation.");
            return false;
        }

        RequireMissingRegistration();
        // Mark ownership before calling OleAut32 so a partial registration is also cleaned up.
        registered = true;
        RegisterTypeLibForUser(library, vendorPath, null);
        return true;
    }

    public void Dispose()
    {
        try
        {
            if (registered)
            {
                var id = attributes.guid;
                UnRegisterTypeLibForUser(ref id, attributes.wMajorVerNum, attributes.wMinorVerNum,
                    attributes.lcid, attributes.syskind);
                RequireMissingRegistration();
            }
        }
        finally { Marshal.ReleaseComObject(library); }
    }
}
