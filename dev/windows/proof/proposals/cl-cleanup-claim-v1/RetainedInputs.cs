// Fixed enrollment controller interop. Compile only under separately approved execution.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;

public sealed class RetainedInputs : IDisposable {
    const uint Read = 0x80000000, ReadAttributes = 0x80, ShareRead = 1;
    const uint OpenExisting = 3, BackupSemantics = 0x02000000, OpenReparsePoint = 0x00200000;
    const uint Reparse = 0x400;
    readonly List<SafeFileHandle> handles = new List<SafeFileHandle>();
    readonly List<Action> checks = new List<Action>();
    readonly HashSet<string> directories = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

    [StructLayout(LayoutKind.Sequential)]
    struct Info {
        public uint Attributes, CreationLow, CreationHigh, AccessLow, AccessHigh;
        public uint WriteLow, WriteHigh, Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security,
        uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetFileInformationByHandle(SafeFileHandle file, out Info info);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern uint GetFinalPathNameByHandleW(SafeFileHandle file, System.Text.StringBuilder path,
        uint length, uint flags);

    static Info Metadata(SafeFileHandle handle, string path) {
        Info info;
        if (!GetFileInformationByHandle(handle, out info)) throw new Win32Exception();
        if ((info.Attributes & Reparse) != 0) throw new IOException("Reparse point: " + path);
        var name = new System.Text.StringBuilder(32768);
        uint length = GetFinalPathNameByHandleW(handle, name, (uint)name.Capacity, 0);
        if (length == 0 || length >= name.Capacity) throw new Win32Exception();
        string expected = @"\\?\" + Path.GetFullPath(path).TrimEnd('\\');
        if (!String.Equals(name.ToString().TrimEnd('\\'), expected, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Final path mismatch: " + path);
        return info;
    }
    SafeFileHandle Open(string path, bool directory) {
        var handle = CreateFileW(path, directory ? ReadAttributes : Read, ShareRead, IntPtr.Zero,
            OpenExisting, OpenReparsePoint | (directory ? BackupSemantics : 0), IntPtr.Zero);
        if (handle.IsInvalid) { handle.Dispose(); throw new Win32Exception(); }
        handles.Add(handle);
        return handle;
    }
    public void PinParents(string path) {
        var parent = new DirectoryInfo(Path.GetDirectoryName(Path.GetFullPath(path)));
        var chain = new Stack<string>();
        while (parent != null) { chain.Push(parent.FullName); parent = parent.Parent; }
        while (chain.Count != 0) {
            string current = chain.Pop();
            if (directories.Add(current)) {
                var handle = Open(current, true);
                Info original = Metadata(handle, current);
                checks.Add(() => {
                    Info later = Metadata(handle, current);
                    if (later.Volume != original.Volume || later.IndexHigh != original.IndexHigh || later.IndexLow != original.IndexLow)
                        throw new IOException("Directory identity mismatch: " + current);
                });
            }
        }
    }
    public void PinFile(string path, string expectedHash, long expectedSize, string expectedId) {
        PinParents(path);
        var handle = Open(path, false);
        checks.Add(() => Verify(handle, path, expectedHash, expectedSize, expectedId));
        Verify(handle, path, expectedHash, expectedSize, expectedId);
    }
    static void Verify(SafeFileHandle handle, string path, string expectedHash, long expectedSize, string expectedId) {
        Info info = Metadata(handle, path);
        string id = info.Volume + ":" + info.IndexHigh + ":" + info.IndexLow;
        long size = ((long)info.SizeHigh << 32) | info.SizeLow;
        if (size != expectedSize || (expectedId != null && id != expectedId))
            throw new IOException("File identity mismatch: " + path);
        using (var borrowed = new SafeFileHandle(handle.DangerousGetHandle(), false))
        using (var stream = new FileStream(borrowed, FileAccess.Read))
        using (var sha = SHA256.Create()) {
            stream.Position = 0;
            string actual = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
            if (!String.Equals(actual, expectedHash, StringComparison.Ordinal))
                throw new IOException("File hash mismatch: " + path);
        }
        Metadata(handle, path);
    }
    public void VerifyAll() { foreach (var check in checks) check(); }
    public string ObserveFile(string path) {
        PinParents(path);
        var handle = Open(path, false);
        Info info = Metadata(handle, path);
        long size = ((long)info.SizeHigh << 32) | info.SizeLow;
        string id = info.Volume + ":" + info.IndexHigh + ":" + info.IndexLow;
        string digest;
        using (var borrowed = new SafeFileHandle(handle.DangerousGetHandle(), false))
        using (var stream = new FileStream(borrowed, FileAccess.Read))
        using (var sha = SHA256.Create()) {
            digest = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
        }
        checks.Add(() => Verify(handle, path, digest, size, id));
        Verify(handle, path, digest, size, id);
        return size + ":" + id + ":" + digest;
    }
    public void Dispose() {
        var failures = new List<Exception>();
        for (int i = handles.Count - 1; i >= 0; --i) {
            var handle = handles[i];
            if (handle.IsClosed || handle.IsInvalid) continue;
            if (!CloseHandle(handle.DangerousGetHandle())) {
                failures.Add(new Win32Exception(Marshal.GetLastWin32Error(), "Retained handle close failed at index " + i));
                continue;
            }
            handle.SetHandleAsInvalid();
            handle.Dispose();
        }
        if (failures.Count != 0) throw new AggregateException("Retained handle close failures", failures);
    }
}
