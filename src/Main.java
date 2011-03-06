// require https://jna.dev.java.net/
// javac -classpath .;jna.jar Main.java && java -classpath .;jna.jar Main

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Platform;
import com.sun.jna.Pointer;
import com.sun.jna.Callback;

public class Main {
    public interface CLibrary extends Library {
        public interface vimremote_eval_f extends Callback {
            int invoke(String expr, Pointer result);
        }

        // linux: without extension "libvimremote.so" is searched.
        CLibrary INSTANCE = (CLibrary)Native.loadLibrary(
                Platform.isWindows() ? "vimremote" : "vimremote.so", CLibrary.class);
        Pointer vimremote_malloc(long len);
        void vimremote_free(Pointer p);
        int vimremote_init();
        int vimremote_uninit();
        int vimremote_serverlist(Pointer[] servernames);
        int vimremote_remoteexpr(String servername, String expr, Pointer[] result);
        int vimremote_register(String servername, vimremote_eval_f eval);
        int vimremote_eventloop(int forever);
    }

    public void command_serverlist() {
        Pointer[] p = new Pointer[1];
        if (CLibrary.INSTANCE.vimremote_serverlist(p) != 0) {
            throw new RuntimeException("vimremote_serverlist() failed");
        }
        String servernames = p[0].getString(0);
        System.out.print(servernames);
        CLibrary.INSTANCE.vimremote_free(p[0]);
    }

    public void command_remoteexpr(String servername, String expr) {
        Pointer[] p = new Pointer[1];
        if (CLibrary.INSTANCE.vimremote_remoteexpr(servername, expr, p) != 0) {
            throw new RuntimeException("vimremote_remoteexpr() failed: " +
                    (p[0] == Pointer.NULL ? "" : p[0].getString(0)));
        }
        String result = p[0].getString(0);
        System.out.println(result);
        CLibrary.INSTANCE.vimremote_free(p[0]);
    }

    public void command_server(String servername) {
        CLibrary.vimremote_eval_f usereval = new CLibrary.vimremote_eval_f() {
            public int invoke(String expr, Pointer result) {
                // eval?
                int ret = 0;
                String res = expr;
                byte[] bytes = res.getBytes();
                Pointer p = CLibrary.INSTANCE.vimremote_malloc(bytes.length + 1);
                p.write(0, bytes, 0, bytes.length);
                p.setByte(bytes.length, (byte)0);
                result.setPointer(0, p);
                return ret;
            }
        };

        if (CLibrary.INSTANCE.vimremote_register(servername, usereval) != 0) {
            throw new RuntimeException("vimremote_register() failed");
        }
        CLibrary.INSTANCE.vimremote_eventloop(1);
    }

    public void usage() {
        System.out.println("vimremote");
        System.out.println("  -h or --help          display help");
        System.out.println("  --serverlist          List available Vim server names");
        System.out.println("  --servername <name>   Vim server name");
        System.out.println("  --remote-expr <expr>  Evaluate <expr> in a Vim server");
        System.out.println("  --server              Start server");
        System.exit(0);
    }

    public void run(String[] args) {
        boolean serverlist = false;
        String servername = "";
        String remoteexpr = "";
        boolean server = false;

        if (args.length == 0) {
            usage();
        }

        for (int i = 0; i < args.length; ++i) {
            if (args[i].equals("--serverlist")) {
                serverlist = true;
            } else if (args[i].equals("--servername")) {
                servername = args[++i];
            } else if (args[i].equals("--remote-expr")) {
                remoteexpr = args[++i];
            } else if (args[i].equals("--server")) {
                server = true;
            } else {
                usage();
            }
        }

        if (CLibrary.INSTANCE.vimremote_init() != 0) {
            throw new RuntimeException("vimremote_init() failed");
        }

        if (serverlist) {
            command_serverlist();
        } else if (!remoteexpr.equals("")) {
            if (servername.equals("")) {
                throw new RuntimeException("remoteexpr requires servername");
            }
            command_remoteexpr(servername, remoteexpr);
        } else if (server) {
            if (servername.equals("")) {
                throw new RuntimeException("server requires servername");
            }
            command_server(servername);
        } else {
            usage();
        }

        if (CLibrary.INSTANCE.vimremote_uninit() != 0) {
            throw new RuntimeException("vimremote_uninit() failed");
        }
    }

    public static void main(String[] args) {
        Main app = new Main();
        app.run(args);
    }

}

