-- pandoc filter for the PDF manual: the GNUmakefile puts a comment
-- "<!-- chapter NAME -->" before each file; the next heading of level 1 gets
-- NAME as its id, and links to NAME.md jump there
function Pandoc( doc )
  local chapter = nil
  local blocks = {}
  for _, b in ipairs( doc.blocks ) do
    local name = b.t == 'RawBlock' and b.text:match( '^<!%-%- chapter (%S+) %-%->$' )
    if name then
      chapter = name
    else
      if b.t == 'Header' and b.level == 1 and chapter then
        b.identifier = 'ch-' .. chapter
        chapter = nil
      end
      table.insert( blocks, b )
    end
  end
  doc.blocks = blocks
  return doc:walk( {
    Link = function( l )
      local target = l.target:match( '([%w%-]+)%.md$' )
      if target then
        l.target = '#ch-' .. target
      end
      return l
    end
  } )
end
