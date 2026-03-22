package t::TestLJ;

use v5.10.1;
use strict;
use warnings;

use Exporter ();
use Cwd qw(cwd);
use File::Temp qw(tempdir);
use IPC::Open3 qw(open3);
use Symbol qw(gensym);

our @ISA = qw(Exporter);
our @EXPORT = qw(blocks plan run_tests);

my $cwd = cwd;
my $lua_cpath = join ';', "$cwd/src/?.so", "$cwd/?.so", ';;';
my $lua_path = join ';', "$cwd/src/?.lua", "$cwd/src/?/?.lua", "$cwd/?/?.lua", ';;';
my $lua_bin = $ENV{TEST_LJ_BIN};
if (!defined $lua_bin || $lua_bin eq '') {
    my $candidate = "$cwd/src/luajit";
    $lua_bin = -x $candidate ? $candidate : 'luajit';
}

$ENV{LUA_CPATH} = $lua_cpath;
$ENV{LUA_PATH} = $lua_path;

my %BLOCK_CACHE;
my $PLANNED = 0;
my $RUN = 0;
my $FAILED = 0;
my %FILE_DEFAULT_REQUIRES;

sub import {
    my ($class, @args) = @_;
    my (undef, $file) = caller;
    my @exports;
    my $default_requires = [];

    while (@args) {
        my $arg = shift @args;
        if ($arg eq 'default_requires') {
            die "default_requires expects a value\n" if !@args;
            $default_requires = _normalize_requires(shift @args);
            next;
        }
        push @exports, $arg;
    }

    $FILE_DEFAULT_REQUIRES{$file} = $default_requires;
    $class->export_to_level(1, $class, @exports ? @exports : @EXPORT);
}

sub plan {
    my %args = @_;
    $PLANNED = $args{tests} // die "plan requires tests => N\n";
    print "1..$PLANNED\n";
    return $PLANNED;
}

sub blocks {
    my $file = $0;
    my $blocks = _active_blocks($file);
    return wantarray ? @{$blocks} : scalar @{$blocks};
}

sub _parse_blocks {
    my ($file) = @_;
    return $BLOCK_CACHE{$file} if $BLOCK_CACHE{$file};

    open my $fh, '<', $file or die "Cannot open $file: $!";
    my $in_data = 0;
    my @blocks;
    my $block;
    my ($section_name, $section_mode, @section_lines);

    my $flush_section = sub {
        return if !$block || !defined $section_name;

        while (@section_lines && $section_lines[-1] =~ /^\s*$/) {
            pop @section_lines;
        }

        my $value = join '', @section_lines;
        if (defined $section_mode && $section_mode eq 'eval') {
            my $evaluated = eval $value;
            die "Failed to eval section $section_name in $file: $@" if $@;
            $block->{$section_name} = $evaluated;
        } else {
            $block->{$section_name} = $value;
        }

        $section_name = undef;
        $section_mode = undef;
        @section_lines = ();
    };

    my $flush_block = sub {
        $flush_section->();
        if ($block) {
            push @blocks, $block;
            $block = undef;
        }
    };

    while (my $line = <$fh>) {
        if (!$in_data) {
            $in_data = 1 if $line =~ /^__DATA__\s*$/;
            next;
        }

        if ($line =~ /^===\s+(.*)\s*$/) {
            $flush_block->();
            $block = { name => $1 };
            next;
        }

        if ($line =~ /^---\s+([A-Za-z_]+)(?:\s+([A-Za-z_]+))?(?::\s*(.*))?$/) {
            $flush_section->();
            my ($name, $mode, $inline) = ($1, $2, $3);
            if (defined $inline) {
                $block->{$name} = $inline;
                next;
            }
            $section_name = $name;
            $section_mode = $mode;
            @section_lines = ();
            next;
        }

        push @section_lines, $line if defined $section_name;
    }

    $flush_block->();
    close $fh;
    $BLOCK_CACHE{$file} = \@blocks;
    return $BLOCK_CACHE{$file};
}

sub _slurp_handle {
    my ($fh) = @_;
    local $/;
    my $content = <$fh>;
    return defined $content ? $content : '';
}

sub _run3 {
    my (@cmd) = @_;
    my $err_fh = gensym;
    my $pid = open3(undef, my $out_fh, $err_fh, @cmd);
    my $stdout = _slurp_handle($out_fh);
    my $stderr = _slurp_handle($err_fh);
    waitpid $pid, 0;
    return ($stdout, $stderr, $?);
}

sub _normalize_requires {
    my ($value) = @_;
    return [] if !defined $value;
    my @items;
    if (ref $value eq 'ARRAY') {
        @items = @{$value};
    } else {
        @items = split /[\s,]+/, $value;
    }
    my %seen;
    return [grep { defined $_ && $_ ne '' && !$seen{$_}++ } @items];
}

sub _active_caps {
    if (defined $ENV{TEST_LJ_CAPS}) {
        my %caps = map { $_ => 1 } @{_normalize_requires($ENV{TEST_LJ_CAPS})};
        return \%caps;
    }

    return undef if !$ENV{TEST_LJ_DISABLE_JIT};
    return {};
}

sub _block_requires {
    my ($file, $block) = @_;
    my @requires = @{_normalize_requires($block->{requires})};
    if (exists $FILE_DEFAULT_REQUIRES{$file}) {
        push @requires, @{$FILE_DEFAULT_REQUIRES{$file}};
    }
    return _normalize_requires(\@requires);
}

sub _requirements_satisfied {
    my ($caps, $requires) = @_;
    return 1 if !defined $caps;
    for my $requirement (@{$requires}) {
        return 0 if !$caps->{$requirement};
    }
    return 1;
}

sub _active_blocks {
    my ($file) = @_;
    my $blocks = _parse_blocks($file);
    my $caps = _active_caps();
    return $blocks if !defined $caps;

    my @filtered = grep { _requirements_satisfied($caps, _block_requires($file, $_)) } @{$blocks};
    return \@filtered;
}

sub _tap_escape {
    my ($value) = @_;
    $value = '' if !defined $value;
    $value =~ s/\n/\n# /g;
    return $value;
}

sub _ok {
    my ($pass, $desc, $diag) = @_;
    $RUN++;
    if ($pass) {
        print "ok $RUN - $desc\n";
        return 1;
    }
    $FAILED++;
    print "not ok $RUN - $desc\n";
    print "# " . _tap_escape($diag) . "\n" if defined $diag && $diag ne '';
    return 0;
}

sub _is {
    my ($got, $expected, $desc) = @_;
    if (defined $got && defined $expected && $got eq $expected) {
        return _ok(1, $desc);
    }
    if (!defined $got && !defined $expected) {
        return _ok(1, $desc);
    }
    my $diag = "got: " . (defined $got ? $got : 'undef') . "\nexpected: " . (defined $expected ? $expected : 'undef');
    return _ok(0, $desc, $diag);
}

sub _like {
    my ($got, $regex, $desc) = @_;
    return _ok(1, $desc) if defined $got && $got =~ $regex;
    my $diag = "got: " . (defined $got ? $got : 'undef') . "\nregex: $regex";
    return _ok(0, $desc, $diag);
}

sub run_test {
    my ($block) = @_;
    my $name = $block->{name};

    my $lua = $block->{lua}
      or die "No --- lua specified for test $name\n";

    my $luafile = 'test.lua';
    my $dir = tempdir 'testlj_XXXXXXX', CLEANUP => 1;
    chdir $dir or die "$name - Cannot chdir to $dir: $!";

    open my $fh, '>', $luafile
      or die "$name - Cannot open $luafile in $dir for writing: $!\n";
    print {$fh} $lua;
    close $fh;

    my @cmd;
    if ($ENV{TEST_LJ_USE_VALGRIND}) {
        warn "$name\n";
        @cmd = (
            'valgrind', '-q', '--leak-check=full', $lua_bin,
            defined($block->{jv}) ? '-jv' : (),
            defined($block->{jdump}) ? '-jdump' : (),
            $luafile,
        );
    } else {
        @cmd = (
            $lua_bin,
            defined($block->{jv}) ? '-jv' : (),
            defined($block->{jdump}) ? '-jdump' : (),
            $luafile,
        );
    }

    my ($res, $err, $rc) = _run3(@cmd);
    my $exp_rc = defined $block->{exit} ? $block->{exit} : 0;

    _is($rc >> 8, $exp_rc, "$name - exit code okay");

    if (exists $block->{err}) {
        my $exp_err = $block->{err};
        if ($err =~ /.*:.*:.*: (.*\s)?/) {
            $err = $1;
        }

        if (ref $exp_err eq 'Regexp') {
            _like($err, $exp_err, "$name - err like expected");
        } else {
            _is($err, $exp_err, "$name - err expected");
        }
    } elsif (defined $err && $err ne '') {
        warn "$name - STDERR:\n$err";
    }

    if (exists $block->{out}) {
        _is($res, $block->{out}, "$name - output ok");
    } elsif (defined $res && $res ne '') {
        warn "$name - STDOUT:\n$res";
    }

    chdir $cwd or die $!;
}

sub run_tests {
    for my $block (@{_active_blocks($0)}) {
        run_test($block);
    }
    if ($PLANNED && $RUN != $PLANNED) {
        _ok(0, 'plan completed', "ran $RUN tests but planned $PLANNED");
    }
    exit($FAILED ? 1 : 0);
}

1;
