
require "optparse"
require "dl/import"

module VimRemote
  extend DL::Importable
  dlload File.join(File.dirname(File.expand_path(__FILE__)), "vimremote.so")
  typealias("size_t", "unsigned long")
  extern "void* vimremote_malloc(size_t)"
  extern "void vimremote_free(void*)"
  extern "int vimremote_init()"
  extern "int vimremote_uninit()"
  extern "int vimremote_serverlist(char**)"
  extern "int vimremote_remoteexpr(const char*, const char*, char**)"
  extern "int vimremote_register(const char*, void*)"
  extern "int vimremote_eventloop(int)"
end

def remote_expr(servername, expr)
  result = DL::PtrData.new(0)
  result.free = VimRemote.extern('void vimremote_free(void*)')
  if VimRemote.vimremote_remoteexpr(servername, expr, result.ref) != 0
    raise "vimremote_remoteexpr() failed: #{result.to_s}"
  end
  return result.to_s
end

def command_serverlist()
  servernames = DL::PtrData.new(0)
  servernames.free = VimRemote.extern('void vimremote_free(void*)')
  if VimRemote.vimremote_serverlist(servernames.ref) != 0
    raise "vimremote_serverlist() failed"
  end
  print servernames.to_s
end

def command_remoteexpr(servername, expr)
  puts remote_expr(servername, expr)
end

def command_server(servername)
  feval = DL.callback('ISP') {|expr, result|
    begin
      res = eval(expr).to_s
      err = 0
    rescue => exc
      res = exc.to_s
      err = -1
    end
    result.struct!('S', 'val')
    result['val'] = VimRemote.vimremote_malloc(res.size + 1)
    result['val'][0, res.size + 1] = res
    err
  }
  if VimRemote.vimremote_register(servername, feval) != 0
    raise "vimremote_register() failed"
  end
  while true
    VimRemote.vimremote_eventloop(0)
    sleep(0.1)
  end
end

def main()
  if VimRemote.vimremote_init() != 0
    raise "vimremote_init() failed"
  end

  opts = {}
  opt = OptionParser.new
  opt.on('--serverlist') {|v| opts[:serverlist] = v }
  opt.on('--servername NAME') {|v| opts[:servername] = v }
  opt.on('--remote-expr EXPR') {|v| opts[:remoteexpr] = v }
  opt.on('--server') {|v| opts[:server] = v}

  opt.parse!(ARGV)

  if opts[:serverlist]
    command_serverlist()
  elsif opts[:remoteexpr]
    if !opts[:servername]
      raise "remoteexpr requires servername"
    end
    command_remoteexpr(opts[:servername], opts[:remoteexpr])
  elsif opts[:server]
    if !opts[:servername]
      raise "server requires servername"
    end
    command_server(opts[:servername])
  else
    print opt.help
  end

  if VimRemote.vimremote_uninit() != 0
    raise "vimremote_uninit() failed"
  end
end

main()
