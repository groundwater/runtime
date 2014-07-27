(function(load, exec){
  var cache = {}

  function normal(path) {
    if (path[0] !== '/')

    return '/' + path
  }

  function require(nodule){
    var path = normal(nodule)

    if (!cache[path]) {
      var src    = load(path)
      var module = {
        exports : {}
      }
      var globals = {
        require : require,
        module  : module,
        exports : module.exports
      }

      exec(globals, path, src)

      cache[path] = module
    }

    return cache[path].exports
  }

  return require
})(load, exec)
