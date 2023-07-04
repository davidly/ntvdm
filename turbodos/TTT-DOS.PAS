{ App to prove you can't win at Tic-Tac-Toe }

program ttt;
{$I timeutil.pas}
{$I dos_gt.pas}

const
  scoreWin = 6;
  scoreTie = 5;
  scoreLose = 4;
  scoreMax = 9;
  scoreMin = 2;

  pieceBlank = 0;
  pieceX = 1;
  pieceO = 2;

  iterations = 100;

type
  boardType = array[ 0..8 ] of integer;
  CommandString = string[127];

var
  evaluated: integer;
  board: boardType;
  timeStart, timeEnd: timetype;

procedure dumpBoard;
var
  i : integer;
begin
  Write( '{' );
  for i := 0 to 8 do
    Write( board[i] );
  Write( '}' );
end;

function lookForWinner : integer;
var
  t, p : integer;
begin
  {  dumpBoard; }
  p := pieceBlank;
  t := board[ 0 ];
  if pieceBlank <> t then
  begin
    if ( ( ( t = board[1] ) and ( t = board[2] ) ) or
         ( ( t = board[3] ) and ( t = board[6] ) ) ) then
      p := t;
  end;

  if pieceBlank = p then
  begin
    t := board[1];
    if ( t = board[4] ) and ( t = board[7] ) then
      p := t
    else
    begin
      t := board[2];
      if ( t = board[5] ) and ( t = board[8] ) then
        p := t
      else
      begin
        t := board[3];
        if ( t = board[4] ) and ( t = board[5] ) then
          p := t
        else
        begin
          t := board[6];
          if ( t = board[7] ) and ( t = board[8] ) then
            p := t
          else
          begin
            t := board[4];
            if ( ( ( t = board[0] ) and ( t = board[8] ) ) or
                 ( ( t = board[2] ) and ( t = board[6] ) ) ) then
              p := t
          end;
        end;
      end;
    end;
  end;

  lookForWinner := p;
end;

function minmax( alpha: integer; beta: integer; depth: integer ): integer;
var
  p, value, pieceMove, score : integer;
  done: boolean;
begin
  evaluated := evaluated + 1;
  value := 0;
  if depth >= 4 then
  begin
    p := lookForWinner;
    if p <> pieceBlank then
    begin
      if p = pieceX then
        value := scoreWin
      else
        value := scoreLose
    end
    else if depth = 8 then
      value := scoreTie;
  end;

  if value = 0 then
  begin
    if ( 0 <> ( depth AND 1 ) ) then
    begin
      value := scoreMin;
      pieceMove := pieceX;
    end
    else
    begin
      value := scoreMax;
      pieceMove := pieceO;
    end;

    done := false;
    p := 0;
    repeat
      if board[ p ] = pieceBlank then
      begin
        board[ p ] := pieceMove;
        score := minmax( alpha, beta, depth + 1 );
        board[ p ] := pieceBlank;

        if ( 0 <> ( depth and 1 ) ) then
        begin
          if ( score > value ) then value := score;
          if ( value > alpha ) then alpha := value;
          if ( alpha >= beta ) or ( value = scoreWin ) then
            done := true;
        end
        else
        begin
          if ( score < value ) then value := score;
          if ( value < beta ) then beta := value;
          if ( beta <= alpha ) or ( value = scoreLose ) then
            done := true;
        end;
      end;
      p := p + 1;
      if p > 8 then done := true;
    until done;
  end;

  minmax := value;
end;

procedure runit( move : integer );
var
  score: integer;
begin
  board[move] := pieceX;
  score := minmax( scoreMin, scoreMax, 0 );
  board[move] := pieceBlank;
end;

function Trim( intext : CommandString ): CommandString;
var FirstPos, LastPos: integer;
begin
  FirstPos := 1;
  while ( FirstPos <= Length( intext ) ) and ( intext[FirstPos] = #32 ) do
    FirstPos := FirstPos + 1;

  LastPos := Length( intext );
  while ( LastPos >= 1 ) and ( intext[LastPos] = #32 ) do
    LastPos := LastPos - 1;

  Trim := Copy( intext, FirstPos, LastPos - FirstPos + 1 );
end;

var
  i, loops, errpos: integer;
  strArg : CommandString;
  cmdTail : CommandString absolute Cseg : $80;
begin
  loops := Iterations;

  { not available on TP v1
  if 0 <> Length( ParamStr( 1 ) ) then Val( ParamStr( 1 ), loops, errpos );
  }

  if Length( cmdTail ) > 0 then
  begin
    { trim the space at the start that's always there }
    strArg := Trim( cmdTail );
    Val( strArg, loops, errpos );
  end;

  for i := 0 to 8 do
    board[i] := pieceBlank;

  get_time( timeStart );

  for i := 1 to loops do
  begin
    evaluated := 0;  { once per loop to prevent overflow }
    runit( 0 );
    runit( 1 );
    runit( 4 );
  end;

  get_time( timeEnd );
  print_elapsed_time( timeStart, timeEnd );

  WriteLn( 'moves evaluated: ', evaluated );
  WriteLn( 'iterations: ', loops );
end.
