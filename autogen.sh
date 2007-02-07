                                                                          
(automake --version) < /dev/null > /dev/null 2>&1 || {
        echo;
        echo "You must have automake installed to compile libjingle";
        echo;
        exit;
}

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
        echo;
        echo "You must have autoconf installed to compile libjingle";
        echo;
        exit;
}

(libtoolize --version) < /dev/null > /dev/null 2>&1 || {
        echo;
        echo "You must have libtool installed to compile libjingle";
        echo;
        exit;
}

echo n | libtoolize --copy --force || exit;
aclocal -I talk || exit;
autoheader || exit;
automake --add-missing --copy;
autoconf || exit;
automake || exit;
./configure $@
