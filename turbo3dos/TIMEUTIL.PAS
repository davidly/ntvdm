type
timetype = record h, m, s, l : integer; end;

regpack = record
          ax,bx,cx,dx,bp,si,di,ds,es,flags: integer;
          end;

procedure time_difference( var tStart, tEnd, tDiff : timetype );
var
  startSecond, startMinute, startHour : integer;

begin { time_difference }
  startSecond := tStart.s;
  startMinute := tStart.m;
  startHour := tStart.h;

  tDiff.l := tEnd.l - tStart.l;
  if ( tDiff.l < 0 ) then
  begin
    tDiff.l := tDiff.l + 100;
    startSecond := startSecond + 1;
  end;

  tDiff.s := tEnd.s - startSecond;
  if ( tDiff.s < 0 ) then
  begin
    tDiff.s := tDiff.s + 60;
    startMinute := startMinute + 1;
  end;

  tDiff.m := tEnd.m - startMinute;
  if ( tDiff.m < 0 ) then
  begin
    tDiff.m := tDiff.m + 60;
    startHour := startHour + 1;
  end;

  tDiff.h := tEnd.h - startHour;
  if ( tDiff.h < 0 ) then
    tDiff.h := tDiff.h + 12;
end;

procedure get_time( var tt : timetype );
var
  recpack:        regpack;
  ah,al,ch,cl,dh: byte;

begin
  ah := $2c;
  with recpack do
  begin
    ax := ah shl 8 + al;
  end;
  intr( $21, recpack );
  with recpack do
  begin
    tt.h := cx shr 8;
    tt.m := cx mod 256;
    tt.s := dx shr 8;
    tt.l := dx mod 256;
  end;
end;

procedure print_time( var t: timetype );

begin
    if ( t.h < 10 ) then write( '0' );
    write( t.h, ':' );
    if ( t.m < 10 ) then write( '0' );
    write( t.m, ':' );
    if ( t.s < 10 ) then write( '0' );
    write( t.s, '.' );
    if ( t.l < 10 ) then write( '0' );
    write( t.l );
end;

procedure print_elapsed_time( var timeStart, timeEnd: timetype );
var
  timeDiff: timetype;
begin
  time_difference( timeStart, timeEnd, timeDiff );
  write( 'elapsed time: ' );
  print_time( timeDiff );
  writeln;
end;
