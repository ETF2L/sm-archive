/**
 * Copyright 2026 ETF2L Contributors, Dave
 *
 * SMArchive (SourceMod compression and archives): compresses and
 * decompresses files and memory with gzip, bzip2, brotli, xz, zstd and lz4,
 * and lists, extracts and creates tar, zip and 7z archives (RAR is read
 * only), in the background.
 *
 * Each operation queues a job and returns its id at once. A pool of worker
 * threads (sm_archive_threads, 2 by default) runs the jobs: they start in
 * the order they were queued, high priority ones first (a normal job that
 * waited 30 seconds counts as high), and end in any order. Jobs queued with
 * serial = true run one after the other in their order, among the serial
 * jobs of the same plugin. The callback runs on the main thread on a later
 * server frame, never during the call that queued the job.
 *
 * Outputs appear only when a job succeeds: a file is written as
 * "<outFile>.<job>.smarchive.tmp" and renamed onto outFile at the end; an
 * archive is extracted into "<targetDir>.<job>.smarchive.tmp/" and renamed
 * onto targetDir. On failure the temp file or staging directory is deleted
 * and the output left as it was (an extraction's targetDir does not exist).
 * Temp files and staging directories that interrupted jobs left behind are
 * deleted by the next job writing the same output.
 *
 * Extraction is safe against hostile archives: entries with absolute paths
 * or ".." parts, names that are not UTF-8, the same path twice and
 * encrypted entries fail the job; symlinks are created only when they lead
 * to a place inside targetDir (judged again once every entry is written: a
 * symlink a later one made lead out fails the job), hardlinks only to files
 * the job extracted, and devices, FIFOs and sockets never. Limits
 * (ArchiveLimits) stop archive bombs.
 *
 * When a plugin unloads, its jobs are cancelled without callbacks. When
 * the extension unloads, every job is stopped and dropped.
 *
 * Paths are relative to the game directory (for Team Fortress 2 the tf/
 * folder) with "/" between their parts. A path is refused with a native
 * error when it is empty, starts with "/" or "file://", contains "\", has
 * an empty part ("a//b", a trailing "/") or a part that is "." or "..", or
 * does not fit into the server's PLATFORM_MAX_PATH (4096 bytes, not
 * SourcePawn's 256) below the game directory. Paths are UTF-8. An array or
 * buffer must hold the length or maxlength passed with it; one beyond the
 * plugin's memory is a native error.
 *
 * Documentation: https://github.com/etf2l/sm-archive
 */

#if defined _smarchive_included
#endinput
#endif
#define _smarchive_included

/**
 * How a job ended. Every value except ArchiveError_None means that no
 * output was written (an existing one is unchanged).
 */
enum ArchiveError {
	ArchiveError_None = 0, /**< Success */
	ArchiveError_OpenInput, /**< The input could not be opened, e.g. it does not exist */
	ArchiveError_OpenOutput, /**< The temp file could not be created, e.g. its directory does not exist */
	ArchiveError_OutputExists, /**< outFile exists and overwrite was false, or targetDir exists */
	ArchiveError_CreatePath, /**< A directory could not be created */
	ArchiveError_Read, /**< Reading the input failed */
	ArchiveError_Write, /**< Writing failed, e.g. the disk is full */
	ArchiveError_Rename, /**< The temp file or staging directory could not be renamed onto the output */
	ArchiveError_Memory, /**< A library ran out of memory */
	ArchiveError_Corrupt, /**< The data is corrupt, or not of the format given */
	ArchiveError_UnexpectedEOF, /**< The input ends in the middle of a stream, or is empty */
	ArchiveError_TrailingData, /**< Data that is no further stream follows the compressed stream */
	ArchiveError_UnknownFormat, /**< Auto could not tell the input's format */
	ArchiveError_Unsupported, /**< The input uses a feature that is not supported, or ReadEntry's entry is no regular file */
	ArchiveError_Encrypted, /**< The archive or an entry is encrypted */
	ArchiveError_EntryName, /**< An entry's name is not valid UTF-8 (Create: or has "\") */
	ArchiveError_UnsafeEntry, /**< An entry is absolute, contains "..", comes twice or collides with another */
	ArchiveError_LimitSize, /**< Unpacking exceeded ArchiveLimits.MaxSize */
	ArchiveError_LimitEntries, /**< The archive has more entries than ArchiveLimits.MaxEntries */
	ArchiveError_LimitRatio, /**< Unpacking exceeded ArchiveLimits.MaxRatio */
	ArchiveError_LimitMemory, /**< Output in memory exceeded ArchiveLimits.MaxMemory */
	ArchiveError_EntryNotFound, /**< ReadEntry: the archive has no such entry */
	ArchiveError_Cancelled, /**< The job was cancelled with Compression.Cancel */
	ArchiveError_Internal /**< A bug in SMArchive or a library; please report it */
};

/** A stream codec. */
enum CodecFormat {
	CodecFormat_Auto = 0, /**< Decompressing: tell it by its magic bytes, brotli (".br") by the name */
	CodecFormat_None,     /**< A tar: plain, not compressed */
	CodecFormat_Gzip,     /**< .gz */
	CodecFormat_Bzip2,    /**< .bz2 */
	CodecFormat_Brotli,   /**< .br */
	CodecFormat_Xz,       /**< .xz */
	CodecFormat_Zstd,     /**< .zst */
	CodecFormat_Lz4       /**< .lz4 (the lz4 frame format) */
};

/** A container. */
enum ArchiveFormat {
	ArchiveFormat_Auto = 0, /**< Tell it by its magic bytes (zip, 7z, RAR, tar) or the name (.tar, .tgz, ...) */
	ArchiveFormat_Tar,      /**< tar, plain or through a codec (.tar.gz, .tar.xz, ...) */
	ArchiveFormat_Zip,
	ArchiveFormat_SevenZip, /**< 7z */
	ArchiveFormat_Rar       /**< RAR 4 and 5, read only */
};

enum ArchiveEntryType {
	ArchiveEntry_File = 0,
	ArchiveEntry_Directory,
	ArchiveEntry_Symlink,
	ArchiveEntry_Hardlink,
	ArchiveEntry_Other /**< A device, FIFO or socket */
};

/** Why an entry was not written. */
enum ArchiveSkip {
	ArchiveSkip_None = 0,          /**< It was written */
	ArchiveSkip_LinkOutside,       /**< A symlink that is absolute or leads out of targetDir */
	ArchiveSkip_LinkTargetMissing, /**< A hardlink to a file the job did not extract */
	ArchiveSkip_SpecialFile,       /**< A device, FIFO or socket */
	ArchiveSkip_SourceSymlink      /**< Create: a symlink among the sources */
};

enum ArchivePriority {
	ArchivePriority_Normal = 0,
	ArchivePriority_High
};

/** How brotli treats its input. */
enum BrotliContent {
	BrotliContent_Generic = 0,
	BrotliContent_Text, /**< UTF-8 text */
	BrotliContent_Font  /**< WOFF 2.0 fonts */
};

/** The integrity check xz stores. */
enum XzCheck {
	XzCheck_None = 0,
	XzCheck_Crc32,
	XzCheck_Crc64,
	XzCheck_Sha256
};

/** The block size of lz4 frames. */
enum Lz4BlockSize {
	Lz4Block_64KiB = 0,
	Lz4Block_256KiB,
	Lz4Block_1MiB,
	Lz4Block_4MiB
};

/** How zip entries are compressed. */
enum ZipMethod {
	ZipMethod_Store = 0,
	ZipMethod_Deflate, /**< What every zip tool reads */
	ZipMethod_Bzip2,
	ZipMethod_Lzma,
	ZipMethod_Xz,
	ZipMethod_Zstd
};

/** How 7z archives are compressed. */
enum SevenZipMethod {
	SevenZipMethod_Copy = 0,
	SevenZipMethod_Deflate,
	SevenZipMethod_Bzip2,
	SevenZipMethod_Lzma1,
	SevenZipMethod_Lzma2, /**< 7-Zip's default */
	SevenZipMethod_Ppmd
};

/**
 * The options of a codec: one of the methodmaps below, each with its codec's
 * settings at their defaults. Delete it when done; a queued job keeps its
 * own copy.
 */
methodmap CodecOptions < Handle {
	/** The codec these options are for. */
	property CodecFormat Format {
		public native get();
	}
}

methodmap GzipOptions < CodecOptions {
	public native GzipOptions();

	/** 1 to 9, default 6. The header holds no name and mtime 0 (gzip -n). */
	property int Level {
		public native get();
		public native set(int level);
	}
}

methodmap Bzip2Options < CodecOptions {
	public native Bzip2Options();

	/** 1 to 9 (block size in units of 100 kB), default 9. */
	property int Level {
		public native get();
		public native set(int level);
	}
}

methodmap BrotliOptions < CodecOptions {
	public native BrotliOptions();

	/** 0 to 11, default 6. */
	property int Quality {
		public native get();
		public native set(int quality);
	}

	/** Window bits 10 to 24, or 0 (default) for one sized from the input, as the brotli tool does. */
	property int Window {
		public native get();
		public native set(int window);
	}

	/** Default BrotliContent_Generic. */
	property BrotliContent Mode {
		public native get();
		public native set(BrotliContent mode);
	}
}

methodmap XzOptions < CodecOptions {
	public native XzOptions();

	/** 0 to 9, default 6. */
	property int Preset {
		public native get();
		public native set(int preset);
	}

	/** xz -e: slower, sometimes smaller; default false. */
	property bool Extreme {
		public native get();
		public native set(bool extreme);
	}

	/** Default XzCheck_Crc64. */
	property XzCheck Check {
		public native get();
		public native set(XzCheck check);
	}
}

methodmap ZstdOptions < CodecOptions {
	public native ZstdOptions();

	/** 1 to 22, default 3. */
	property int Level {
		public native get();
		public native set(int level);
	}

	/**
	 * Long distance matching's window in bits, 10 to 27, or 0 (default) for
	 * off. Decompression takes windows up to 27 bits.
	 */
	property int LongWindow {
		public native get();
		public native set(int bits);
	}

	/** Store a checksum of the content; default true. */
	property bool Checksum {
		public native get();
		public native set(bool checksum);
	}
}

methodmap Lz4Options < CodecOptions {
	public native Lz4Options();

	/** 1 to 12, default 1; 3 and above use lz4's high compression mode. */
	property int Level {
		public native get();
		public native set(int level);
	}

	/** Default Lz4Block_4MiB. */
	property Lz4BlockSize BlockSize {
		public native get();
		public native set(Lz4BlockSize size);
	}

	/** Store a checksum of the content; default true. */
	property bool ContentChecksum {
		public native get();
		public native set(bool checksum);
	}
}

/**
 * How an archive is written: TarOptions, ZipOptions or SevenZipOptions.
 * Delete it when done; a queued job keeps its own copy.
 */
methodmap ArchiveWriteOptions < Handle {
	property ArchiveFormat Format {
		public native get();
	}
}

/** A tar in the pax format, plain or compressed with a codec. */
methodmap TarOptions < ArchiveWriteOptions {
	/**
	 * @param codec  The codec to compress the tar with, null for a plain
	 *               tar. Its settings are copied; codec can be deleted after.
	 */
	public native TarOptions(CodecOptions codec = null);

	/** CodecFormat_None for a plain tar. */
	property CodecFormat Codec {
		public native get();
	}
}

methodmap ZipOptions < ArchiveWriteOptions {
	public native ZipOptions();

	/** Default ZipMethod_Deflate. */
	property ZipMethod Method {
		public native get();
		public native set(ZipMethod method);
	}

	/** 0 to 9, default 6; ignored by ZipMethod_Store. */
	property int Level {
		public native get();
		public native set(int level);
	}
}

methodmap SevenZipOptions < ArchiveWriteOptions {
	public native SevenZipOptions();

	/** Default SevenZipMethod_Lzma2. */
	property SevenZipMethod Method {
		public native get();
		public native set(SevenZipMethod method);
	}

	/** 0 to 9, default 5; ignored by SevenZipMethod_Copy. */
	property int Level {
		public native get();
		public native set(int level);
	}
}

/**
 * What unpacking (decompression, listing, extraction, ReadEntry) may
 * produce; compressing into memory is held to MaxMemory only. A new
 * ArchiveLimits starts with the server's defaults (the sm_archive_max_*
 * cvars); set any value, 0 for no limit. Jobs queued without limits use the
 * defaults. Delete it when done; a queued job keeps its own copy.
 */
methodmap ArchiveLimits < Handle {
	public native ArchiveLimits();

	/**
	 * Bytes written in total, and for a tar through a codec also the bytes
	 * the codec decodes, skipped entries included; default 1 GiB.
	 */
	property int MaxSize {
		public native get();
		public native set(int bytes);
	}
	public native void GetMaxSize64(int bytes[2]);
	public native void SetMaxSize64(const int bytes[2]);

	/** Entries of an archive, every kind counted; default 10000. */
	property int MaxEntries {
		public native get();
		public native set(int entries);
	}

	/**
	 * Bytes written per byte read, overall and per entry, judged once more
	 * than 16 MiB were written, and for a tar through a codec also the bytes
	 * decoded per byte read; default 1000.
	 */
	property int MaxRatio {
		public native get();
		public native set(int ratio);
	}

	/** Bytes of output kept in memory (memory jobs, ReadEntry); default 64 MiB. */
	property int MaxMemory {
		public native get();
		public native set(int bytes);
	}
	public native void GetMaxMemory64(int bytes[2]);
	public native void SetMaxMemory64(const int bytes[2]);
}

/**
 * What goes into an archive Archive.Create writes. Symlinks and special
 * files among the sources are skipped (and reported); a name in a source
 * directory that is not UTF-8 or contains "\" fails the job with
 * ArchiveError_EntryName. Delete it when done; a queued job keeps its own
 * copy.
 */
methodmap ArchiveSources < Handle {
	public native ArchiveSources();

	/**
	 * @param gamePath     A file below the game directory.
	 * @param archivePath  Its path in the archive, e.g. "logs/L1121.log":
	 *                     relative, "/" between parts, no empty, "." or
	 *                     ".." parts.
	 * @error              A path that breaks the rules, or an empty
	 *                     archivePath.
	 */
	public native void AddFile(const char[] gamePath, const char[] archivePath);

	/**
	 * Adds a directory with everything in it.
	 *
	 * @param gamePath     A directory below the game directory.
	 * @param archivePath  Where it goes in the archive; "" puts its content
	 *                     at the archive's root.
	 * @error              A path that breaks the rules.
	 */
	public native void AddDirectory(const char[] gamePath, const char[] archivePath = "");

	property int Length {
		public native get();
	}
}

/**
 * The prefixes that select what Archive.Extract writes: entries at or
 * below one of them ("addons" takes "addons" and "addons/...", not
 * "addons2"). Delete it when done; a queued job keeps its own copy.
 */
methodmap ArchivePrefixes < Handle {
	public native ArchivePrefixes();

	/**
	 * @param prefix  An archive path, e.g. "addons/sourcemod": relative, "/"
	 *                between parts, no empty, "." or ".." parts.
	 * @error         An empty prefix or one that breaks the rules.
	 */
	public native void Add(const char[] prefix);

	property int Length {
		public native get();
	}
}

/**
 * What a job reports: how it ended, its entries and its output in memory.
 */
methodmap ArchiveResult < Handle {
	property ArchiveError Error {
		public native get();
	}

	/** The failing library's own error code, 0 when none applies. */
	property int LibraryCode {
		public native get();
	}

	/** The job's id, as the operation returned it. */
	property int Job {
		public native get();
	}

	/** The codec read or written; CodecFormat_None when there was none. */
	property CodecFormat Codec {
		public native get();
	}

	/** The container read or written; ArchiveFormat_Auto when there was none. */
	property ArchiveFormat Format {
		public native get();
	}

	/** What went wrong, in English, for logs; empty on success. */
	public native int GetMessage(char[] buffer, int maxlength);

	/**
	 * List, Extract, ReadEntry and Create: the number of entries. Extract
	 * reports the entries it selected, Create what it put in the archive
	 * and the skipped sources. 0 when the job failed.
	 */
	property int EntryCount {
		public native get();
	}

	/** The entry's normalized path, e.g. "addons/sourcemod/plugins/x.smx". */
	public native int GetEntryPath(int index, char[] buffer, int maxlength);
	public native ArchiveEntryType GetEntryType(int index);

	/** Unpacked size in bytes, -1 when the archive does not tell it. */
	public native int GetEntrySize(int index);
	public native void GetEntrySize64(int index, int size[2]);

	/**
	 * Archive bytes read for a file while it was extracted (tar and zip; 0
	 * for small entries libarchive read ahead); -1 when not known: other
	 * entry types, List, ReadEntry, Create, 7z and RAR.
	 */
	public native int GetEntryPackedSize(int index);
	public native void GetEntryPackedSize64(int index, int size[2]);

	/** Modification time in seconds since 1970, 0 when the archive has none. */
	public native int GetEntryTime(int index);
	public native bool IsEntryExecutable(int index);

	/** A symlink's or hardlink's target as stored. */
	public native int GetEntryLinkTarget(int index, char[] buffer, int maxlength);

	/** Extract and Create: whether the entry was written. */
	public native bool IsEntryWritten(int index);
	public native ArchiveSkip GetEntrySkip(int index);

	/** Memory output and ReadEntry: the number of bytes; 0 when the job failed. */
	property int Size {
		public native get();
	}
	public native void GetSize64(int size[2]);

	/**
	 * Copies the bytes as a string, as many as fit before the terminating
	 * null. A null byte in them ends the string early for SourcePawn.
	 *
	 * @return  The number of bytes copied.
	 */
	public native int GetString(char[] buffer, int maxlength);

	/**
	 * Copies bytes, one byte (0 to 255) per cell.
	 *
	 * @param offset  The first byte to copy.
	 * @return        The number of bytes copied.
	 */
	public native int GetBytes(int[] bytes, int maxlength, int offset = 0);

	/** The buffer size GetBase64 needs, the terminating null included. */
	property int Base64Size {
		public native get();
	}

	/**
	 * Writes the bytes as base64 (RFC 4648, padded).
	 *
	 * @error  A buffer smaller than Base64Size.
	 */
	public native int GetBase64(char[] buffer, int maxlength);

	/** A copy that lives after the callback; delete it when done. */
	public native ArchiveResult Clone();
}

/**
 * Called on the main thread when a job has ended.
 *
 * @param error   How the job ended, the same as result.Error.
 * @param result  Everything the job reports. It is valid only during the
 *                callback and closed after it; Clone() keeps a copy.
 * @param data    The data passed when the job was queued.
 */
typedef ArchiveCallback = function void (ArchiveError error, ArchiveResult result, any data);

/**
 * Called on the main thread while a job runs, at most 4 times a second and
 * only when something changed. Values beyond 2 GiB saturate at 2147483647.
 *
 * @param job           The job's id.
 * @param bytesRead     Bytes read from the input.
 * @param bytesWritten  Bytes written to the output.
 * @param entries       Archive entries done.
 * @param inputSize     The input's size, -1 while unknown.
 * @param data          The data passed when the job was queued.
 */
typedef ArchiveProgress = function void (int job, int bytesRead, int bytesWritten, int entries, int inputSize,
                                         any data);

/**
 * Compressing and decompressing files and memory, and the jobs.
 *
 * Common parameters:
 *   callback   Called when the job has ended.
 *   data       Any value, passed on to the callbacks.
 *   overwrite  true: replace outFile if it exists. false: the job ends with
 *              ArchiveError_OutputExists and outFile is not touched.
 *   createPath true: create outFile's missing directories (mode 0775 less
 *              the umask). false: a missing directory ends the job with
 *              ArchiveError_OpenOutput.
 *   serial     true: run after the plugin's earlier serial jobs.
 *   priority   ArchivePriority_High starts before normal jobs.
 *   limits     The limits of unpacking (of compressing into memory only
 *              MaxMemory); null for the server's defaults.
 *   progress   Called while the job runs; INVALID_FUNCTION for none.
 *
 * Every operation returns the job's id and throws a native error, queueing
 * nothing, for a path that breaks the rules, an invalid handle, value or
 * callback.
 */
methodmap Compression {
	/**
	 * Queues the compression of a file, e.g. "demos/x.dem" to
	 * "demos/x.dem.zst" with a ZstdOptions.
	 */
	public static native int CompressFile(const char[] inFile, const char[] outFile, CodecOptions options,
	                                      ArchiveCallback callback, any data = 0, bool overwrite = false,
	                                      bool createPath = true, bool serial = false,
	                                      ArchivePriority priority = ArchivePriority_Normal,
	                                      ArchiveProgress progress = INVALID_FUNCTION);

	/**
	 * Queues the decompression of a file. A file of several streams one
	 * after the other (gzip members, bzip2 or xz streams, zstd or lz4 frames)
	 * is decompressed whole; anything else after the stream fails with
	 * ArchiveError_TrailingData.
	 *
	 * @param format  The codec, or CodecFormat_Auto to tell it.
	 */
	public static native int DecompressFile(const char[] inFile, const char[] outFile, ArchiveCallback callback,
	                                        any data = 0, CodecFormat format = CodecFormat_Auto,
	                                        bool overwrite = false, bool createPath = true, bool serial = false,
	                                        ArchivePriority priority = ArchivePriority_Normal,
	                                        ArchiveLimits limits = null,
	                                        ArchiveProgress progress = INVALID_FUNCTION);

	/**
	 * Queues the compression of a string (without its terminating null).
	 * The result holds the compressed bytes (GetBytes, GetBase64), or they
	 * go to outFile when one is given.
	 */
	public static native int CompressString(const char[] text, CodecOptions options, ArchiveCallback callback,
	                                        any data = 0, const char[] outFile = "", bool overwrite = false,
	                                        bool createPath = true, bool serial = false,
	                                        ArchivePriority priority = ArchivePriority_Normal,
	                                        ArchiveLimits limits = null,
	                                        ArchiveProgress progress = INVALID_FUNCTION);

	/** As CompressString, of length bytes, one byte (0 to 255) per cell. */
	public static native int CompressBytes(const int[] bytes, int length, CodecOptions options,
	                                       ArchiveCallback callback, any data = 0, const char[] outFile = "",
	                                       bool overwrite = false, bool createPath = true, bool serial = false,
	                                       ArchivePriority priority = ArchivePriority_Normal,
	                                       ArchiveLimits limits = null,
	                                       ArchiveProgress progress = INVALID_FUNCTION);

	/**
	 * Queues the decompression of length bytes, one byte per cell. The
	 * result holds the bytes (GetString, GetBytes), or they go to outFile.
	 * CodecFormat_Auto tells the codec by its magic bytes (not brotli).
	 */
	public static native int DecompressBytes(const int[] bytes, int length, ArchiveCallback callback, any data = 0,
	                                         CodecFormat format = CodecFormat_Auto, const char[] outFile = "",
	                                         bool overwrite = false, bool createPath = true, bool serial = false,
	                                         ArchivePriority priority = ArchivePriority_Normal,
	                                         ArchiveLimits limits = null,
	                                         ArchiveProgress progress = INVALID_FUNCTION);

	/** As DecompressBytes, of base64 text (RFC 4648, padded, no whitespace). */
	public static native int DecompressBase64(const char[] base64, ArchiveCallback callback, any data = 0,
	                                          CodecFormat format = CodecFormat_Auto, const char[] outFile = "",
	                                          bool overwrite = false, bool createPath = true, bool serial = false,
	                                          ArchivePriority priority = ArchivePriority_Normal,
	                                          ArchiveLimits limits = null,
	                                          ArchiveProgress progress = INVALID_FUNCTION);

	/**
	 * Cancels a job of this plugin: a queued one is dropped, a running one
	 * stops at its next chunk. Its callback still runs, with
	 * ArchiveError_Cancelled, unless it finished meanwhile.
	 *
	 * @return  false when the plugin has no such job (any more).
	 */
	public static native bool Cancel(int job);

	/**
	 * Writes the name of an ArchiveError, e.g. "ArchiveError_Corrupt".
	 *
	 * @return  The number of bytes written.
	 * @error   A value that is no ArchiveError.
	 */
	public static native int ErrorString(ArchiveError error, char[] buffer, int maxlength);
}

/**
 * Listing, extracting and creating archives. The common parameters are
 * those of Compression, and:
 *   format  The container, or ArchiveFormat_Auto to tell it.
 *   codec   For ArchiveFormat_Tar: its codec, CodecFormat_Auto to tell it
 *           or CodecFormat_None for a plain tar. ArchiveFormat_Auto takes
 *           only CodecFormat_Auto; zip, 7z and RAR take CodecFormat_Auto or
 *           CodecFormat_None.
 */
methodmap Archive {
	/**
	 * Queues the listing of an archive's entries (in the result), with the
	 * checks of extraction on their names and number. Nothing is written, so
	 * link collisions are only found by Extract; the data a tar through a
	 * codec holds is decoded and counts against MaxSize and MaxRatio.
	 */
	public static native int List(const char[] archive, ArchiveCallback callback, any data = 0,
	                              ArchiveFormat format = ArchiveFormat_Auto, CodecFormat codec = CodecFormat_Auto,
	                              bool serial = false, ArchivePriority priority = ArchivePriority_Normal,
	                              ArchiveLimits limits = null, ArchiveProgress progress = INVALID_FUNCTION);

	/**
	 * Queues the extraction of an archive into targetDir, which must not
	 * exist; it appears only when everything was extracted. Files are
	 * created 0664 (0775 when the archive marks them executable) and
	 * directories 0775, less the umask, with the archive's times.
	 *
	 * @param targetDir  The directory to create.
	 * @param prefixes   Only entries at or below one of these are extracted
	 *                   and reported; null for every entry.
	 * @param createPath true: create targetDir's missing parent directories.
	 *                   false: a missing parent ends the job with
	 *                   ArchiveError_CreatePath.
	 */
	public static native int Extract(const char[] archive, const char[] targetDir, ArchiveCallback callback,
	                                 any data = 0, ArchiveFormat format = ArchiveFormat_Auto,
	                                 CodecFormat codec = CodecFormat_Auto, ArchivePrefixes prefixes = null,
	                                 bool createPath = true, bool serial = false,
	                                 ArchivePriority priority = ArchivePriority_Normal, ArchiveLimits limits = null,
	                                 ArchiveProgress progress = INVALID_FUNCTION);

	/**
	 * Queues the reading of one regular file of an archive into memory (the
	 * result's bytes, within ArchiveLimits.MaxMemory), e.g. a manifest. An
	 * entry that is no regular file fails with ArchiveError_Unsupported, a
	 * missing one with ArchiveError_EntryNotFound.
	 *
	 * @param entryPath  Its path in the archive, as GetEntryPath gives it.
	 */
	public static native int ReadEntry(const char[] archive, const char[] entryPath, ArchiveCallback callback,
	                                   any data = 0, ArchiveFormat format = ArchiveFormat_Auto,
	                                   CodecFormat codec = CodecFormat_Auto, bool serial = false,
	                                   ArchivePriority priority = ArchivePriority_Normal, ArchiveLimits limits = null,
	                                   ArchiveProgress progress = INVALID_FUNCTION);

	/**
	 * Queues the creation of an archive from sources. Two sources for the
	 * same archive path fail the job with ArchiveError_UnsafeEntry.
	 *
	 * @error  An empty sources.
	 */
	public static native int Create(const char[] outFile, ArchiveSources sources, ArchiveWriteOptions options,
	                                ArchiveCallback callback, any data = 0, bool overwrite = false,
	                                bool createPath = true, bool serial = false,
	                                ArchivePriority priority = ArchivePriority_Normal,
	                                ArchiveProgress progress = INVALID_FUNCTION);
}

/**
 * Do not edit below this line!
 */
public Extension __ext_smarchive = {
	name = "SourceMod compression and archives",
	file = "smarchive.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_smarchive_SetNTVOptional() {
	MarkNativeAsOptional("CodecOptions.Format.get");
	MarkNativeAsOptional("GzipOptions.GzipOptions");
	MarkNativeAsOptional("GzipOptions.Level.get");
	MarkNativeAsOptional("GzipOptions.Level.set");
	MarkNativeAsOptional("Bzip2Options.Bzip2Options");
	MarkNativeAsOptional("Bzip2Options.Level.get");
	MarkNativeAsOptional("Bzip2Options.Level.set");
	MarkNativeAsOptional("BrotliOptions.BrotliOptions");
	MarkNativeAsOptional("BrotliOptions.Quality.get");
	MarkNativeAsOptional("BrotliOptions.Quality.set");
	MarkNativeAsOptional("BrotliOptions.Window.get");
	MarkNativeAsOptional("BrotliOptions.Window.set");
	MarkNativeAsOptional("BrotliOptions.Mode.get");
	MarkNativeAsOptional("BrotliOptions.Mode.set");
	MarkNativeAsOptional("XzOptions.XzOptions");
	MarkNativeAsOptional("XzOptions.Preset.get");
	MarkNativeAsOptional("XzOptions.Preset.set");
	MarkNativeAsOptional("XzOptions.Extreme.get");
	MarkNativeAsOptional("XzOptions.Extreme.set");
	MarkNativeAsOptional("XzOptions.Check.get");
	MarkNativeAsOptional("XzOptions.Check.set");
	MarkNativeAsOptional("ZstdOptions.ZstdOptions");
	MarkNativeAsOptional("ZstdOptions.Level.get");
	MarkNativeAsOptional("ZstdOptions.Level.set");
	MarkNativeAsOptional("ZstdOptions.LongWindow.get");
	MarkNativeAsOptional("ZstdOptions.LongWindow.set");
	MarkNativeAsOptional("ZstdOptions.Checksum.get");
	MarkNativeAsOptional("ZstdOptions.Checksum.set");
	MarkNativeAsOptional("Lz4Options.Lz4Options");
	MarkNativeAsOptional("Lz4Options.Level.get");
	MarkNativeAsOptional("Lz4Options.Level.set");
	MarkNativeAsOptional("Lz4Options.BlockSize.get");
	MarkNativeAsOptional("Lz4Options.BlockSize.set");
	MarkNativeAsOptional("Lz4Options.ContentChecksum.get");
	MarkNativeAsOptional("Lz4Options.ContentChecksum.set");
	MarkNativeAsOptional("ArchiveWriteOptions.Format.get");
	MarkNativeAsOptional("TarOptions.TarOptions");
	MarkNativeAsOptional("TarOptions.Codec.get");
	MarkNativeAsOptional("ZipOptions.ZipOptions");
	MarkNativeAsOptional("ZipOptions.Method.get");
	MarkNativeAsOptional("ZipOptions.Method.set");
	MarkNativeAsOptional("ZipOptions.Level.get");
	MarkNativeAsOptional("ZipOptions.Level.set");
	MarkNativeAsOptional("SevenZipOptions.SevenZipOptions");
	MarkNativeAsOptional("SevenZipOptions.Method.get");
	MarkNativeAsOptional("SevenZipOptions.Method.set");
	MarkNativeAsOptional("SevenZipOptions.Level.get");
	MarkNativeAsOptional("SevenZipOptions.Level.set");
	MarkNativeAsOptional("ArchiveLimits.ArchiveLimits");
	MarkNativeAsOptional("ArchiveLimits.MaxSize.get");
	MarkNativeAsOptional("ArchiveLimits.MaxSize.set");
	MarkNativeAsOptional("ArchiveLimits.GetMaxSize64");
	MarkNativeAsOptional("ArchiveLimits.SetMaxSize64");
	MarkNativeAsOptional("ArchiveLimits.MaxEntries.get");
	MarkNativeAsOptional("ArchiveLimits.MaxEntries.set");
	MarkNativeAsOptional("ArchiveLimits.MaxRatio.get");
	MarkNativeAsOptional("ArchiveLimits.MaxRatio.set");
	MarkNativeAsOptional("ArchiveLimits.MaxMemory.get");
	MarkNativeAsOptional("ArchiveLimits.MaxMemory.set");
	MarkNativeAsOptional("ArchiveLimits.GetMaxMemory64");
	MarkNativeAsOptional("ArchiveLimits.SetMaxMemory64");
	MarkNativeAsOptional("ArchiveSources.ArchiveSources");
	MarkNativeAsOptional("ArchiveSources.AddFile");
	MarkNativeAsOptional("ArchiveSources.AddDirectory");
	MarkNativeAsOptional("ArchiveSources.Length.get");
	MarkNativeAsOptional("ArchivePrefixes.ArchivePrefixes");
	MarkNativeAsOptional("ArchivePrefixes.Add");
	MarkNativeAsOptional("ArchivePrefixes.Length.get");
	MarkNativeAsOptional("ArchiveResult.Error.get");
	MarkNativeAsOptional("ArchiveResult.LibraryCode.get");
	MarkNativeAsOptional("ArchiveResult.Job.get");
	MarkNativeAsOptional("ArchiveResult.Codec.get");
	MarkNativeAsOptional("ArchiveResult.Format.get");
	MarkNativeAsOptional("ArchiveResult.GetMessage");
	MarkNativeAsOptional("ArchiveResult.EntryCount.get");
	MarkNativeAsOptional("ArchiveResult.GetEntryPath");
	MarkNativeAsOptional("ArchiveResult.GetEntryType");
	MarkNativeAsOptional("ArchiveResult.GetEntrySize");
	MarkNativeAsOptional("ArchiveResult.GetEntrySize64");
	MarkNativeAsOptional("ArchiveResult.GetEntryPackedSize");
	MarkNativeAsOptional("ArchiveResult.GetEntryPackedSize64");
	MarkNativeAsOptional("ArchiveResult.GetEntryTime");
	MarkNativeAsOptional("ArchiveResult.IsEntryExecutable");
	MarkNativeAsOptional("ArchiveResult.GetEntryLinkTarget");
	MarkNativeAsOptional("ArchiveResult.IsEntryWritten");
	MarkNativeAsOptional("ArchiveResult.GetEntrySkip");
	MarkNativeAsOptional("ArchiveResult.Size.get");
	MarkNativeAsOptional("ArchiveResult.GetSize64");
	MarkNativeAsOptional("ArchiveResult.GetString");
	MarkNativeAsOptional("ArchiveResult.GetBytes");
	MarkNativeAsOptional("ArchiveResult.Base64Size.get");
	MarkNativeAsOptional("ArchiveResult.GetBase64");
	MarkNativeAsOptional("ArchiveResult.Clone");
	MarkNativeAsOptional("Compression.CompressFile");
	MarkNativeAsOptional("Compression.DecompressFile");
	MarkNativeAsOptional("Compression.CompressString");
	MarkNativeAsOptional("Compression.CompressBytes");
	MarkNativeAsOptional("Compression.DecompressBytes");
	MarkNativeAsOptional("Compression.DecompressBase64");
	MarkNativeAsOptional("Compression.Cancel");
	MarkNativeAsOptional("Compression.ErrorString");
	MarkNativeAsOptional("Archive.List");
	MarkNativeAsOptional("Archive.Extract");
	MarkNativeAsOptional("Archive.ReadEntry");
	MarkNativeAsOptional("Archive.Create");
}
#endif
