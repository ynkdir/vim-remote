#!r6rs
;; racket

(import (rnrs)
        (rnrs eval (6))
        (only (racket) sleep string->bytes/utf-8 bytes-length)
        (ffi unsafe))

(define vimremote
  (ffi-lib "vimremote"))

(define vimremote-send-f
  (_fun _string/utf-8 -> _int))

(define vimremote-expr-f
  (_fun _string/utf-8 _pointer -> _int))

(define vimremote-malloc
  (get-ffi-obj "vimremote_malloc" vimremote (_fun _uintptr -> _pointer)))

(define vimremote-free
  (get-ffi-obj "vimremote_free" vimremote (_fun _pointer -> _void)))

(define vimremote-init
  (get-ffi-obj "vimremote_init" vimremote (_fun -> _int)))

(define vimremote-uninit
  (get-ffi-obj "vimremote_uninit" vimremote (_fun -> _int)))

(define vimremote-serverlist
  (get-ffi-obj "vimremote_serverlist" vimremote (_fun _pointer -> _int)))

(define vimremote-remotesend
  (get-ffi-obj "vimremote_remotesend" vimremote (_fun _string/utf-8 _string/utf-8 -> _int)))

(define vimremote-remoteexpr
  (get-ffi-obj "vimremote_remoteexpr" vimremote (_fun _string/utf-8 _string/utf-8 _pointer -> _int)))

(define vimremote-register
  (get-ffi-obj "vimremote_register" vimremote (_fun _string/utf-8 vimremote-send-f vimremote-expr-f -> _int)))

(define vimremote-eventloop
  (get-ffi-obj "vimremote_eventloop" vimremote (_fun _int -> _int)))

(define (command-serverlist)
  (define p (malloc _pointer 1))
  (memset p 0 (ctype-sizeof _pointer))
  (unless (equal? (vimremote-serverlist p) 0)
    (raise "vimremote_serverlist() failed"))
  (display (ptr-ref p _string/utf-8))
  (vimremote-free (ptr-ref p _pointer))
  ; cause crasn. not needed?
  ;(free p)
  )

(define (command-remotesend servername keys)
  (unless (equal? (vimremote-remotesend servername keys) 0)
    (raise "vimremote_remotesend() failed"))
  )

(define (command-remoteexpr servername expr)
  (define p (malloc _pointer 1))
  (unless (equal? (vimremote-remoteexpr servername expr p) 0)
    (raise
      (string-append "vimremote_remoteexpr() failed: "
                     (if (ptr-equal? (ptr-ref p _pointer) #f)
                       ""
                       (ptr-ref p _string/utf-8)))))
  (display (ptr-ref p _string/utf-8))(newline)
  (vimremote-free (ptr-ref p _pointer))
  )

(define (fsend keys)
  (display keys)(newline)
  0)

;; FIXME: eval?
(define (feval expr result)
  (define (set-result s)
    (let* ((b (string->bytes/utf-8 s))
           (p (vimremote-malloc (+ (bytes-length b) 1))))
      (memmove p b (bytes-length b))
      (ptr-set! p _byte (bytes-length b) 0)
      (ptr-set! result _pointer p)))
  (guard (con
           (else
             (when (message-condition? con)
               (set-result (condition-message con)))
             -1))
    (let* ((x (read (open-string-input-port expr)))
           (r (eval x (environment '(rnrs))))
           (s (let-values (((port proc) (open-string-output-port)))
                (display r port)
                (proc))))
      (set-result s)
      0)))

(define (command-server servername)
  (unless (equal? (vimremote-register servername fsend feval) 0)
    (raise "vimremote_register() failed"))
  (let loop ()
    (vimremote-eventloop 0)
    (sleep 0.1)
    (loop))
  )

(define (usage)
  (display "vimremote\n");
  (display "  -h or --help          display help\n");
  (display "  --serverlist          List available Vim server names\n");
  (display "  --servername <name>   Vim server name\n");
  (display "  --remote-send <keys>  Send <keys> to a Vim server and exit\n");
  (display "  --remote-expr <expr>  Evaluate <expr> in a Vim server\n");
  (display "  --server              Start server\n");
  (exit)
  )

(define (main)
  (define args (command-line))
  (define serverlist #f)
  (define servername #f)
  (define remotesend #f)
  (define remoteexpr #f)
  (define server #f)

  (when (equal? (length args) 1)
    (usage))

  (let loop ((i 1))
    (unless (>= i (length args))
      (cond ((equal? (list-ref args i) "--serverlist")
             (set! serverlist #t)
             (loop (+ i 1)))
            ((equal? (list-ref args i) "--servername")
             (set! servername (list-ref args (+ i 1)))
             (loop (+ i 2)))
            ((equal? (list-ref args i) "--remote-send")
             (set! remotesend (list-ref args (+ i 1)))
             (loop (+ i 2)))
            ((equal? (list-ref args i) "--remote-expr")
             (set! remoteexpr (list-ref args (+ i 1)))
             (loop (+ i 2)))
            ((equal? (list-ref args i) "--server")
             (set! server #t)
             (loop (+ i 1)))
            (else
              (usage)))))

  (unless (equal? (vimremote-init) 0)
    (raise "vimremote_init() failed"))

  (cond (serverlist
         (command-serverlist))
        (remotesend
         (unless servername
           (raise "remotesend requires servername"))
         (command-remotesend servername remotesend))
        (remoteexpr
         (unless servername
           (raise "remoteexpr requires servername"))
         (command-remoteexpr servername remoteexpr))
        (server
         (unless servername
           (raise "server requires servername"))
         (command-server servername))
        )

  (unless (equal? (vimremote-uninit) 0)
    (raise "vimremote_uninit() failed"))
  )

(main)
