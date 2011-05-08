// windows: csc main.cs /unsafe
//   linux: gmcs main.cs /unsafe /define:LINUX

using System;
using System.Runtime.InteropServices;

class Application {

#if LINUX
  public const string vimremote = "vimremote.so";
#else
  public const string vimremote = "vimremote.dll";
#endif

  public unsafe delegate int vimremote_eval_f(string expr, byte** result);

  [DllImport(vimremote)]
  public static unsafe extern byte* vimremote_malloc(UIntPtr len);

  [DllImport(vimremote)]
  public static unsafe extern void vimremote_free(byte* p);

  [DllImport(vimremote)]
  public static extern int vimremote_init();

  [DllImport(vimremote)]
  public static extern int vimremote_uninit();

  [DllImport(vimremote)]
  public static unsafe extern int vimremote_serverlist(byte** servernames);

  [DllImport(vimremote)]
  public static unsafe extern int vimremote_remotesend(string servername, string expr);

  [DllImport(vimremote)]
  public static unsafe extern int vimremote_remoteexpr(string servername, string expr, byte** result);

  [DllImport(vimremote)]
  public static unsafe extern int vimremote_register(string servername, vimremote_eval_f eval);

  [DllImport(vimremote)]
  public static unsafe extern int vimremote_eventloop(int forever);

  public System.Text.Encoding enc = new System.Text.UTF8Encoding(true, true);

  public unsafe int strlen(byte* p) {
    int len = 0;
    while (*p++ != 0) {
      len++;
    }
    return len;
  }

  public unsafe byte* strsave(string str) {
    byte[] bytes = enc.GetBytes(str);
    byte* p = vimremote_malloc((UIntPtr)(bytes.Length + 1));
    for (int i = 0; i < bytes.Length; i++) {
      p[i] = bytes[i];
    }
    p[bytes.Length] = 0;
    return p;
  }

  public unsafe void command_serverlist() {
    byte* p = null;
    if (vimremote_serverlist(&p) != 0) {
      throw new Exception("vimremote_serverlist() failed");
    }
    var servernames = new string((sbyte*)p, 0, strlen(p), enc);
    vimremote_free(p);
    System.Console.Write(servernames);
  }

  public unsafe void command_remotesend(string servername, string keys) {
    if (vimremote_remotesend(servername, keys) != 0) {
      throw new Exception("vimremote_remotesend() failed");
    }
  }

  public unsafe void command_remoteexpr(string servername, string expr) {
    byte* p = null;
    if (vimremote_remoteexpr(servername, expr, &p) != 0) {
      var msg = (p == null ? "" : new string((sbyte*)p, 0, strlen(p), enc));
      vimremote_free(p);
      throw new Exception("vimremote_remoteexpr() failed: " + msg);
    }
    var result = new string((sbyte*)p, 0, strlen(p), enc);
    vimremote_free(p);
    System.Console.WriteLine(result);
  }

  public unsafe void command_server(string servername) {
    if (vimremote_register(servername, eval) != 0) {
      throw new Exception("vimremote_register() failed");
    }
    vimremote_eventloop(1);
  }

  // eval?
  public unsafe int eval(string expr, byte** result) {
    System.Console.WriteLine(expr);
    *result = strsave(expr);
    return 0;
  }

  public void usage() {
    System.Console.WriteLine("vimremote");
    System.Console.WriteLine("  -h or --help          display help");
    System.Console.WriteLine("  --serverlist          List available Vim server names");
    System.Console.WriteLine("  --servername <name>   Vim server name");
    System.Console.WriteLine("  --remote-send <keys>  Send <keys> to a Vim server and exit");
    System.Console.WriteLine("  --remote-expr <expr>  Evaluate <expr> in a Vim server");
    System.Console.WriteLine("  --server              Start server");
    Environment.Exit(0);
  }

  public void run(string[] args) {
    bool serverlist = false;
    string servername = null;
    string remotesend = null;
    string remoteexpr = null;
    bool server = false;

    if (args.Length == 0) {
      usage();
    }

    for (int i = 0; i < args.Length; ++i) {
      if (args[i] == "--serverlist") {
        serverlist = true;
      } else if (args[i] == "--servername") {
        servername = args[++i];
      } else if (args[i] == "--remote-send") {
        remotesend = args[++i];
      } else if (args[i] == "--remote-expr") {
        remoteexpr = args[++i];
      } else if (args[i] == "--server") {
        server = true;
      } else {
        usage();
      }
    }

    if (vimremote_init() != 0) {
      throw new Exception("vimremote_init() failed");
    }

    if (serverlist) {
      command_serverlist();
    } else if (remotesend != null) {
      if (servername == null) {
        throw new Exception("remotesend requires servername");
      }
      command_remotesend(servername, remotesend);
    } else if (remoteexpr != null) {
      if (servername == null) {
        throw new Exception("remoteexpr requires servername");
      }
      command_remoteexpr(servername, remoteexpr);
    } else if (server) {
      if (servername == null) {
        throw new Exception("server requires servername");
      }
      command_server(servername);
    }

    if (vimremote_uninit() != 0) {
      throw new Exception("vimremote_uninit() failed");
    }
  }

  public static void Main(string[] args) {
    var app = new Application();
    app.run(args);
  }

}

