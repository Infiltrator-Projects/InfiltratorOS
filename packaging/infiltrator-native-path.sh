# InfiltratorOS native PATH.
case ":$PATH:" in
  *:/Programs:*) ;;
  *) PATH="/Programs:$PATH" ;;
esac
case ":$PATH:" in
  *:/System/Commands:*) ;;
  *) PATH="/System/Commands:$PATH" ;;
esac
export PATH
